"""Verify the embedded manifest's descriptor and mapping after package fixups."""
import pathlib
import struct
import sys


def check_manifest(path):
    with pathlib.Path(path).open("rb") as binary:
        def read(offset, size):
            binary.seek(offset)
            value = binary.read(size)
            assert len(value) == size, "Truncated executable"
            return value

        header = read(0, 64)
        sections = {}
        mappings = []
        image_base = 0
        if header[:4] == b"\x7fELF":
            assert header[4:6] == b"\x02\x01", "Expected little-endian ELF64"
            phoff, shoff = struct.unpack_from("<QQ", header, 32)
            phsize, phnum, shsize, shnum, strindex = struct.unpack_from("<HHHHH", header, 54)
            raw_sections = [struct.unpack("<IIQQQQIIQQ", read(shoff + i * shsize, 64))
                            for i in range(shnum)]
            strings = read(raw_sections[strindex][4], raw_sections[strindex][5])
            for section in raw_sections:
                name = strings[section[0]:].split(b"\0", 1)[0].decode()
                sections[name] = (section[3], section[4], section[5])
            for i in range(phnum):
                segment = struct.unpack("<IIQQQQQQ", read(phoff + i * phsize, 56))
                if segment[0] == 1:  # PT_LOAD
                    mappings.append((segment[3], segment[2], segment[5]))
            descriptor = sections["symdbh"]
            manifest = sections["symdb"]
        else:
            assert header[:4] == b"\xcf\xfa\xed\xfe", "Expected Mach-O64"
            ncmds = struct.unpack_from("<I", header, 16)[0]
            offset = 32
            for _ in range(ncmds):
                command, size = struct.unpack("<II", read(offset, 8))
                if command == 0x19:  # LC_SEGMENT_64
                    segment = read(offset, size)
                    vmaddr, _, fileoff, filesize = struct.unpack_from("<QQQQ", segment, 24)
                    mappings.append((vmaddr, fileoff, filesize))
                    count = struct.unpack_from("<I", segment, 64)[0]
                    for i in range(count):
                        start = 72 + i * 80
                        name = segment[start:start + 16].split(b"\0", 1)[0].decode()
                        addr, length, filepos = struct.unpack_from("<QQI", segment, start + 32)
                        sections[name] = (addr, filepos, length)
                offset += size
            descriptor = sections["__symdbh"]
            manifest = sections["__symdb"]
        magic, rva, length = struct.unpack("<8sQQ", read(descriptor[1], 24))
        assert magic == b"SYMDBHDR", "Invalid descriptor magic"
        assert rva + image_base == manifest[0], "Descriptor address changed during fixups"
        assert length == manifest[2] and length > 72, "Manifest size mismatch"
        assert read(manifest[1], 8) == b"SYMGEN\0\0", "Invalid manifest payload"
        assert any(fileoff <= manifest[1] and manifest[1] + length <= fileoff + size
                   and addr + manifest[1] - fileoff == manifest[0]
                   for addr, fileoff, size in mappings), "Manifest is not mapped at its runtime address"
        print("Embedded manifest descriptor and runtime mapping verified")


if __name__ == "__main__":
    check_manifest(sys.argv[1])
