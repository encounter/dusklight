//! `emit-manifest`: post-link symbol manifest for the game binary.
//!
//! Layout (little-endian, memory-mapped by the loader; see MODS_LINKING.md §3.C):
//!
//! ```text
//! Header  { magic "DUSKMAN\0", version u32, entry_count u32,
//!           build_id_len u32, build_id [u8;32], reserved u32,
//!           strings_off u64, strings_len u64 }              (72 bytes: entries 8-aligned)
//! Entry   { hash u64, rva u64, name_off u32, flags u32 }  × entry_count,
//!           sorted by (hash, name_off) — binary-search by hash, resolve
//!           collisions by comparing the name.
//! Strings NUL-terminated names, referenced by name_off.
//! ```
//!
//! The build id keys the manifest to the exact binary: PDB GUID+age on Windows,
//! LC_UUID on Mach-O, GNU build-id on ELF. A stale manifest fails loudly at load.
//! RVAs are relative to the format's image base (`relative_address_base`); the
//! loader adds the module's runtime base.
//!
//! Symbol sources: on Windows the PDB (publics + per-module procedure/data records,
//! which is how static functions become hookable); elsewhere the binary's own symtab.

use std::collections::BTreeMap;
use std::fs;

use pdb::FallibleIterator;

pub const MAGIC: &[u8; 8] = b"DUSKMAN\0";
pub const VERSION: u32 = 1;

pub const FLAG_CODE: u32 = 1 << 0;
pub const FLAG_DATA: u32 = 1 << 1;
/// Not externally visible in the linked image (PDB module symbol / local symtab entry):
/// hookable via the manifest, never linkable.
pub const FLAG_LOCAL: u32 = 1 << 2;
/// Multiple names resolved to this RVA (ICF fold or alias): a hook here intercepts
/// every folded function.
pub const FLAG_MULTI_NAME: u32 = 1 << 3;
/// This name maps to more than one RVA (internal-linkage statics with the same name in
/// different TUs). Every RVA is present; a by-name lookup must treat it as ambiguous.
pub const FLAG_DUP_NAME: u32 = 1 << 4;

pub fn fnv1a64(bytes: &[u8]) -> u64 {
    let mut hash: u64 = 0xcbf29ce484222325;
    for &b in bytes {
        hash ^= u64::from(b);
        hash = hash.wrapping_mul(0x100000001b3);
    }
    hash
}

/// Compiler-generated locals that are never hook or link targets; keeping them out
/// keeps the manifest lean and the DUP_NAME set meaningful.
fn is_manifest_noise(name: &str) -> bool {
    const PREFIXES: &[&str] = &[
        "ltmp", "l_", "L_",            // assembler-local labels
        "__cxx_global_var_init",       // dynamic initializer pieces
        "_GLOBAL__sub_I",              // TU initializer drivers
        "GCC_except_table", "__GCC_except_table",
        "__unnamed_",                  // anonymous globals
        "OUTLINED_FUNCTION_",          // linker/compiler outlining artifacts
        "$",                           // MSVC pdata/xdata labels
        "__imp_",                      // import pointers (PDB publics carry these)
        "??_C@", "__real@", "__xmm@", "__ymm@", // literals/constants
    ];
    PREFIXES.iter().any(|p| name.starts_with(p))
}

pub struct ManifestSymbol {
    pub name: String,
    pub rva: u64,
    pub flags: u32,
}

pub struct ManifestInput {
    pub build_id: Vec<u8>,
    pub symbols: Vec<ManifestSymbol>,
}

pub fn write_manifest(input: &ManifestInput, out: &str) -> Result<(usize, usize), String> {
    // Dedup model: (name, rva) pairs merge their flags — publics and module records
    // overlap in PDBs, and a public record clears LOCAL. One name at several RVAs is
    // *normal* for internal-linkage statics repeated across TUs: every RVA is kept and
    // the entries are flagged DUP_NAME so by-name lookup knows it's ambiguous. A
    // *global* with two RVAs is an anomaly worth a warning.
    let mut by_name: BTreeMap<&str, Vec<(u64, u32)>> = BTreeMap::new();
    for sym in &input.symbols {
        let rvas = by_name.entry(&sym.name).or_default();
        match rvas.iter_mut().find(|(rva, _)| *rva == sym.rva) {
            Some((_, flags)) => {
                if sym.flags & FLAG_LOCAL == 0 {
                    *flags &= !FLAG_LOCAL;
                }
                *flags |= sym.flags & !FLAG_LOCAL;
            }
            None => rvas.push((sym.rva, sym.flags)),
        }
    }

    let mut rva_names: BTreeMap<u64, u32> = BTreeMap::new();
    for rvas in by_name.values() {
        for (rva, _) in rvas {
            *rva_names.entry(*rva).or_insert(0) += 1;
        }
    }

    // Names at several RVAs are expected, not anomalies: internal-linkage statics
    // repeat across TUs, and PDB module records carry display names without parameter
    // lists, so C++ overloads collide. DUP_NAME marks them ambiguous for by-name lookup.
    let mut entries: Vec<(u64, u64, u32, u32)> = Vec::with_capacity(by_name.len());
    let mut strings: Vec<u8> = Vec::new();
    let mut dup_names = 0usize;
    for (name, rvas) in &by_name {
        let dup = rvas.len() > 1;
        if dup {
            dup_names += 1;
        }
        let name_off = u32::try_from(strings.len())
            .map_err(|_| "string table exceeds 4 GiB".to_string())?;
        strings.extend_from_slice(name.as_bytes());
        strings.push(0);
        let hash = fnv1a64(name.as_bytes());
        for (rva, flags) in rvas {
            let mut flags = *flags;
            if dup {
                flags |= FLAG_DUP_NAME;
            }
            if rva_names.get(rva).copied().unwrap_or(0) > 1 {
                flags |= FLAG_MULTI_NAME;
            }
            entries.push((hash, *rva, name_off, flags));
        }
    }
    entries.sort_unstable_by_key(|&(hash, rva, name_off, _)| (hash, name_off, rva));
    if dup_names != 0 {
        eprintln!("dusk-symgen: {dup_names} names have multiple addresses (flagged DUP_NAME)");
    }

    let build_id_len = input.build_id.len().min(32);
    let header_len = 8 + 4 + 4 + 4 + 32 + 4 + 8 + 8;
    let entries_len = entries.len() * 24;
    let strings_off = (header_len + entries_len) as u64;

    let mut out_bytes: Vec<u8> = Vec::with_capacity(header_len + entries_len + strings.len());
    out_bytes.extend_from_slice(MAGIC);
    out_bytes.extend_from_slice(&VERSION.to_le_bytes());
    out_bytes.extend_from_slice(&u32::try_from(entries.len()).unwrap().to_le_bytes());
    out_bytes.extend_from_slice(&u32::try_from(build_id_len).unwrap().to_le_bytes());
    let mut build_id = [0u8; 32];
    build_id[..build_id_len].copy_from_slice(&input.build_id[..build_id_len]);
    out_bytes.extend_from_slice(&build_id);
    out_bytes.extend_from_slice(&0u32.to_le_bytes()); // reserved: 8-align the entries
    out_bytes.extend_from_slice(&strings_off.to_le_bytes());
    out_bytes.extend_from_slice(&(strings.len() as u64).to_le_bytes());
    for (hash, rva, name_off, flags) in &entries {
        out_bytes.extend_from_slice(&hash.to_le_bytes());
        out_bytes.extend_from_slice(&rva.to_le_bytes());
        out_bytes.extend_from_slice(&name_off.to_le_bytes());
        out_bytes.extend_from_slice(&flags.to_le_bytes());
    }
    out_bytes.extend_from_slice(&strings);
    fs::write(out, &out_bytes).map_err(|e| format!("{out}: {e}"))?;
    Ok((entries.len(), out_bytes.len()))
}

/// Symbols + build id from a linked Mach-O / ELF binary's symtab.
pub fn read_native_binary(path: &str) -> Result<ManifestInput, String> {
    use object::{Object, ObjectSymbol};

    let data = fs::read(path).map_err(|e| format!("{path}: {e}"))?;
    let file = object::File::parse(&*data).map_err(|e| format!("{path}: {e}"))?;

    let build_id = if let Ok(Some(uuid)) = file.mach_uuid() {
        uuid.to_vec()
    } else if let Ok(Some(id)) = file.build_id() {
        id.to_vec()
    } else {
        return Err(format!(
            "{path}: no Mach-O UUID or GNU build-id — cannot key the manifest to the binary"
        ));
    };

    let base = file.relative_address_base();
    let mut symbols = Vec::new();
    for sym in file.symbols() {
        if !sym.is_definition() {
            continue;
        }
        let Ok(name) = sym.name() else { continue };
        if name.is_empty() || is_manifest_noise(name) {
            continue;
        }
        let flags = match sym.kind() {
            object::SymbolKind::Text => FLAG_CODE,
            object::SymbolKind::Data => FLAG_DATA,
            _ => continue,
        } | if sym.is_local() { FLAG_LOCAL } else { 0 };
        // dlsym-convention names: strip the Mach-O leading underscore so lookups
        // use the same spelling on every platform.
        let name = name.strip_prefix('_').unwrap_or(name);
        symbols.push(ManifestSymbol {
            name: name.to_string(),
            rva: sym.address().wrapping_sub(base),
            flags,
        });
    }
    Ok(ManifestInput { build_id, symbols })
}

/// Symbols + build id from a PDB: publics (linkable surface) plus per-module
/// procedure/data records (statics — hookable but not linkable).
pub fn read_pdb(path: &str) -> Result<ManifestInput, String> {
    let file = fs::File::open(path).map_err(|e| format!("{path}: {e}"))?;
    let mut pdb = pdb::PDB::open(file).map_err(|e| format!("{path}: {e}"))?;
    let info = pdb.pdb_information().map_err(|e| format!("{path}: {e}"))?;
    let dbi = pdb.debug_information().map_err(|e| format!("{path}: {e}"))?;
    // The codeview debug directory in the EXE carries GUID + DBI age (falls back to
    // the PDB-info age when the DBI stream has none).
    let age = dbi.age().unwrap_or(info.age);
    let mut build_id = Vec::with_capacity(20);
    build_id.extend_from_slice(info.guid.as_bytes());
    build_id.extend_from_slice(&age.to_le_bytes());

    let address_map = pdb.address_map().map_err(|e| format!("{path}: {e}"))?;
    let mut symbols = Vec::new();

    let globals = pdb.global_symbols().map_err(|e| format!("{path}: {e}"))?;
    let mut iter = globals.iter();
    while let Some(symbol) = iter.next().map_err(|e| format!("{path}: {e}"))? {
        let Ok(data) = symbol.parse() else { continue };
        if let pdb::SymbolData::Public(public) = data {
            let Some(rva) = public.offset.to_rva(&address_map) else { continue };
            let name = public.name.to_string().into_owned();
            if is_manifest_noise(&name) {
                continue;
            }
            let flags = if public.function { FLAG_CODE } else { FLAG_DATA };
            symbols.push(ManifestSymbol { name, rva: u64::from(rva.0), flags });
        }
    }

    let mut modules = dbi.modules().map_err(|e| format!("{path}: {e}"))?;
    while let Some(module) = modules.next().map_err(|e| format!("{path}: {e}"))? {
        let Some(module_info) = pdb.module_info(&module).map_err(|e| format!("{path}: {e}"))?
        else {
            continue;
        };
        let mut sym_iter = module_info.symbols().map_err(|e| format!("{path}: {e}"))?;
        while let Some(symbol) = sym_iter.next().map_err(|e| format!("{path}: {e}"))? {
            let Ok(data) = symbol.parse() else { continue };
            let (name, offset, flags) = match data {
                pdb::SymbolData::Procedure(proc) => {
                    let local = if proc.global { 0 } else { FLAG_LOCAL };
                    (proc.name, proc.offset, FLAG_CODE | local)
                }
                pdb::SymbolData::Data(data_sym) => {
                    let local = if data_sym.global { 0 } else { FLAG_LOCAL };
                    (data_sym.name, data_sym.offset, FLAG_DATA | local)
                }
                _ => continue,
            };
            let Some(rva) = offset.to_rva(&address_map) else { continue };
            if rva.0 == 0 {
                continue; // stripped/discarded contribution
            }
            let name = name.to_string().into_owned();
            if is_manifest_noise(&name) {
                continue;
            }
            symbols.push(ManifestSymbol { name, rva: u64::from(rva.0), flags });
        }
    }

    Ok(ManifestInput { build_id, symbols })
}

/// Verify every export in a generated .def resolves in the manifest (shared-front-end
/// coverage check: emit-def and emit-manifest must agree on the linkable surface).
pub fn verify_def(input: &ManifestInput, def_path: &str) -> Result<(), String> {
    let def = fs::read_to_string(def_path).map_err(|e| format!("{def_path}: {e}"))?;
    let names: std::collections::HashSet<&str> =
        input.symbols.iter().map(|s| s.name.as_str()).collect();
    let mut missing = 0usize;
    let mut total = 0usize;
    for line in def.lines() {
        let line = line.trim();
        if line.is_empty() || line == "EXPORTS" {
            continue;
        }
        let name = line.split_whitespace().next().unwrap_or("");
        total += 1;
        if !names.contains(name) {
            if missing < 20 {
                eprintln!("dusk-symgen: def export missing from manifest: {name}");
            }
            missing += 1;
        }
    }
    if missing != 0 {
        return Err(format!(
            "{missing} of {total} def exports missing from the manifest — emit-def and \
             emit-manifest disagree on the linkable surface"
        ));
    }
    eprintln!("dusk-symgen: def coverage OK ({total} exports all present in manifest)");
    Ok(())
}
