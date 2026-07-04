//! dusk-symgen: the Dusklight symbol pipeline tool.
//!
//! `emit-def` scans built COFF objects/archives with provenance filtering and writes the
//! curated .def for the game executable (the direct game-code ABI surface for mods).
//! `emit-manifest` runs post-link and maps the whole hookable symbol surface (including
//! statics) to RVAs, keyed to the exact build. See MODS_LINKING.md for the design.

mod manifest;

use std::collections::BTreeMap;
use std::fs;
use std::process::ExitCode;

use object::pe;
use object::read::archive::ArchiveFile;
use object::read::coff::{CoffFile, CoffHeader};
use object::{Object, ObjectComdat, ObjectSection, ObjectSymbol, SectionIndex, SectionKind};

type CoffBigFile<'data> = CoffFile<'data, &'data [u8], pe::AnonObjectHeaderBigobj>;

#[derive(Default)]
struct Options {
    rsp: Option<String>,
    sdk_libs: Vec<String>,
    includes: Vec<String>,
    excludes: Vec<String>,
    out: Option<String>,
    report: Option<String>,
    max_exports: usize,
    binary: Option<String>,
    pdb: Option<String>,
    verify_def: Option<String>,
}

#[derive(Default)]
struct Stats {
    objects: usize,
    skipped_comdat: usize,
    skipped_name: usize,
    skipped_path: usize,
    sdk_skipped_name: usize,
}

fn norm(path: &str) -> String {
    path.replace('\\', "/")
}

/// Compiler/runtime symbols that are never part of the game ABI, even when not COMDAT.
fn is_noise_name(name: &str) -> bool {
    const PREFIXES: &[&str] = &[
        "??_R",   // RTTI descriptors: MSVC compares by name string across images
        "??_C@",  // string literals
        "??__E", "??__F", // dynamic initializer / atexit thunks
        "__real@", "__xmm@", "__ymm@", // float constants
        "_CT??", "_TI",   // EH catchable/throw info
        "__imp_", // import pointers never originate here
        "$",      // pdata/xdata labels
        ".",      // section-name-like symbols
        "@",      // @feat.00, @comp.id
        "aurora_", // Aurora's public C API is not part of the mod surface
    ];
    // std:: instantiations are never ABI (mods get theirs from the STL). dusk:: symbols are
    // deliberately NOT filtered: TARGET_PC inline code in game headers calls dusk:: helpers
    // (e.g. frame_interp::lookup_replacement), so the ones defined in game TUs are de-facto
    // game ABI — matching macOS, where every dusk:: symbol is dynamically visible anyway.
    const CONTAINS: &[&str] = &["?$TSS", "@std@@"];
    // Entry points are never mod ABI.
    const EXACT: &[&str] = &["main", "SDL_main", "WinMain", "wWinMain", "DllMain"];
    PREFIXES.iter().any(|p| name.starts_with(p))
        || CONTAINS.iter().any(|c| name.contains(c))
        || EXACT.contains(&name)
}

/// COMDAT sections whose members must not be exported: selectany duplicates
/// (inline functions, templates, vtables) exist in every image; exporting them is
/// noise at best and cross-image identity confusion at worst. NoDuplicates COMDATs
/// (/Gy or -ffunction-sections style) are unique definitions and stay exportable.
fn comdat_excluded_sections<'data, Coff: CoffHeader>(
    file: &CoffFile<'data, &'data [u8], Coff>,
) -> Vec<bool> {
    let max = file.sections().count() + 1;
    let mut excluded = vec![false; max + 1];
    for comdat in file.comdats() {
        if comdat.kind() == object::ComdatKind::NoDuplicates {
            continue;
        }
        for section in comdat.sections() {
            if section.0 < excluded.len() {
                excluded[section.0] = true;
            }
        }
    }
    excluded
}

fn scan_coff(
    data: &[u8],
    c_only: bool,
    exports: &mut BTreeMap<String, bool>,
    stats: &mut Stats,
) -> Result<(), object::Error> {
    // MSVC emits both classic COFF and /bigobj extended COFF objects.
    match CoffFile::<&[u8]>::parse(data) {
        Ok(file) => scan_coff_file(&file, c_only, exports, stats),
        Err(_) => {
            let file = CoffBigFile::parse(data)?;
            scan_coff_file(&file, c_only, exports, stats);
        }
    }
    Ok(())
}

fn scan_coff_file<'data, Coff: CoffHeader>(
    file: &CoffFile<'data, &'data [u8], Coff>,
    c_only: bool,
    exports: &mut BTreeMap<String, bool>,
    stats: &mut Stats,
) {
    stats.objects += 1;
    let excluded = comdat_excluded_sections(&file);

    for sym in file.symbols() {
        if !sym.is_definition() || !sym.is_global() {
            continue;
        }
        let Some(section_index) = sym.section_index() else {
            continue; // absolute/common
        };
        let Ok(name) = sym.name() else { continue };
        if name.is_empty() {
            continue;
        }
        if c_only && name.starts_with('?') {
            stats.sdk_skipped_name += 1;
            continue;
        }
        if is_noise_name(name) {
            stats.skipped_name += 1;
            continue;
        }
        if section_index.0 < excluded.len() && excluded[section_index.0] {
            stats.skipped_comdat += 1;
            continue;
        }
        let is_code = file
            .section_by_index(SectionIndex(section_index.0))
            .map(|s| s.kind() == SectionKind::Text)
            .unwrap_or(false);
        // First classification wins; flag data/code disagreement loudly.
        if let Some(prev) = exports.get(name) {
            if *prev != !is_code {
                eprintln!("dusk-symgen: warning: {name} classified as both code and data");
            }
            continue;
        }
        exports.insert(name.to_string(), !is_code);
    }
}

fn scan_path(
    path: &str,
    c_only: bool,
    exports: &mut BTreeMap<String, bool>,
    stats: &mut Stats,
) -> Result<(), String> {
    let data = fs::read(path).map_err(|e| format!("{path}: {e}"))?;
    if data.starts_with(b"!<arch>") {
        let archive = ArchiveFile::parse(&*data).map_err(|e| format!("{path}: {e}"))?;
        for member in archive.members() {
            let member = member.map_err(|e| format!("{path}: {e}"))?;
            let member_data = member.data(&*data).map_err(|e| format!("{path}: {e}"))?;
            // Import libraries and empty members aren't COFF objects; skip quietly.
            if scan_coff(member_data, c_only, exports, stats).is_err() {
                continue;
            }
        }
        Ok(())
    } else {
        scan_coff(&data, c_only, exports, stats).map_err(|e| format!("{path}: {e}"))
    }
}

fn path_included(path: &str, opts: &Options) -> bool {
    let p = norm(path);
    if opts.excludes.iter().any(|e| p.contains(e.as_str())) {
        return false;
    }
    opts.includes.is_empty() || opts.includes.iter().any(|i| p.contains(i.as_str()))
}

fn run_emit_def(opts: &Options) -> Result<(), String> {
    let out = opts.out.as_deref().ok_or("emit-def: missing --out")?;
    let rsp = opts.rsp.as_deref().ok_or("emit-def: missing --rsp")?;

    let mut exports: BTreeMap<String, bool> = BTreeMap::new();
    let mut stats = Stats::default();

    let rsp_data = fs::read_to_string(rsp).map_err(|e| format!("{rsp}: {e}"))?;
    for line in rsp_data.lines().flat_map(|l| l.split(';')) {
        let path = line.trim();
        if path.is_empty() {
            continue;
        }
        if norm(path).to_lowercase().ends_with(".res") {
            continue; // compiled resources ride along in $<TARGET_OBJECTS>
        }
        if !path_included(path, opts) {
            stats.skipped_path += 1;
            continue;
        }
        scan_path(path, false, &mut exports, &mut stats)?;
    }
    for lib in &opts.sdk_libs {
        scan_path(lib, true, &mut exports, &mut stats)?;
    }

    let mut def = String::from("EXPORTS\n");
    let mut data_count = 0usize;
    for (name, is_data) in &exports {
        if *is_data {
            data_count += 1;
            def.push_str(&format!("    {name} DATA\n"));
        } else {
            def.push_str(&format!("    {name}\n"));
        }
    }
    fs::write(out, def).map_err(|e| format!("{out}: {e}"))?;

    let summary = format!(
        "dusk-symgen emit-def: {} exports ({} data) from {} objects\n\
         skipped: {} selectany-comdat, {} noise-name, {} sdk-non-C, {} path-filtered\n",
        exports.len(),
        data_count,
        stats.objects,
        stats.skipped_comdat,
        stats.skipped_name,
        stats.sdk_skipped_name,
        stats.skipped_path,
    );
    eprint!("{summary}");
    if let Some(report) = opts.report.as_deref() {
        fs::write(report, &summary).map_err(|e| format!("{report}: {e}"))?;
    }

    if opts.max_exports != 0 && exports.len() > opts.max_exports {
        return Err(format!(
            "export count {} exceeds --max-exports {} (PE hard limit is 65535 — act now, \
             see MODS_LINKING.md §3.C for the stub/GOT contingency)",
            exports.len(),
            opts.max_exports
        ));
    }
    Ok(())
}

fn run_emit_manifest(opts: &Options) -> Result<(), String> {
    let out = opts.out.as_deref().ok_or("emit-manifest: missing --out")?;
    let input = match (&opts.pdb, &opts.binary) {
        (Some(pdb), _) => manifest::read_pdb(pdb)?,
        (None, Some(binary)) => manifest::read_native_binary(binary)?,
        (None, None) => return Err("emit-manifest: need --pdb (Windows) or --binary".into()),
    };
    if let Some(def) = opts.verify_def.as_deref() {
        manifest::verify_def(&input, def)?;
    }
    let (entries, bytes) = manifest::write_manifest(&input, out)?;
    eprintln!(
        "dusk-symgen emit-manifest: {entries} symbols ({} raw records), {bytes} bytes, build id {}",
        input.symbols.len(),
        input.build_id.iter().map(|b| format!("{b:02x}")).collect::<String>(),
    );
    Ok(())
}

fn main() -> ExitCode {
    let mut args = std::env::args().skip(1);
    let Some(cmd) = args.next() else {
        eprintln!(
            "usage: dusk-symgen emit-def --rsp <file> --out <def> [--sdk-lib <lib>]... \
             [--include <substr>]... [--exclude <substr>]... [--max-exports <n>] [--report <file>]\n\
             \x20      dusk-symgen emit-manifest (--pdb <pdb> | --binary <bin>) --out <manifest> \
             [--verify-def <def>]"
        );
        return ExitCode::FAILURE;
    };
    let mut opts = Options { max_exports: 60000, ..Default::default() };
    while let Some(arg) = args.next() {
        let mut value = |name: &str| args.next().ok_or(format!("missing value for {name}"));
        let result = match arg.as_str() {
            "--rsp" => value("--rsp").map(|v| opts.rsp = Some(v)),
            "--out" => value("--out").map(|v| opts.out = Some(v)),
            "--report" => value("--report").map(|v| opts.report = Some(v)),
            "--sdk-lib" => value("--sdk-lib").map(|v| opts.sdk_libs.push(v)),
            "--include" => value("--include").map(|v| opts.includes.push(norm(&v))),
            "--exclude" => value("--exclude").map(|v| opts.excludes.push(norm(&v))),
            "--max-exports" => value("--max-exports").and_then(|v| {
                v.parse().map(|n| opts.max_exports = n).map_err(|e| format!("--max-exports: {e}"))
            }),
            "--binary" => value("--binary").map(|v| opts.binary = Some(v)),
            "--pdb" => value("--pdb").map(|v| opts.pdb = Some(v)),
            "--verify-def" => value("--verify-def").map(|v| opts.verify_def = Some(v)),
            other => Err(format!("unknown argument: {other}")),
        };
        if let Err(e) = result {
            eprintln!("dusk-symgen: {e}");
            return ExitCode::FAILURE;
        }
    }

    let result = match cmd.as_str() {
        "emit-def" => run_emit_def(&opts),
        "emit-manifest" => run_emit_manifest(&opts),
        other => Err(format!("unknown subcommand: {other}")),
    };
    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("dusk-symgen: error: {e}");
            ExitCode::FAILURE
        }
    }
}
