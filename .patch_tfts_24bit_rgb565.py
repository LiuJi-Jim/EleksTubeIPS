from pathlib import Path

Import("env")

path = Path("src/TFTs.cpp")

old = """          case 24:
            b = *inputPtr++;
            g = *inputPtr++;
            r = *inputPtr++;
            break;
"""

new = """          case 24:
            // BMP stores 24-bit pixels as B, G, R bytes.
            // Convert 8-bit channels to RGB565 widths before packing below.
            b = (*inputPtr++) >> 3;
            g = (*inputPtr++) >> 2;
            r = (*inputPtr++) >> 3;
            break;
"""

content = path.read_text()

if old in content:
    path.write_text(content.replace(old, new))
    print("Patched TFTs.cpp 24-bit BMP RGB565 conversion")
elif new in content:
    print("TFTs.cpp 24-bit BMP RGB565 conversion already patched")
else:
    print("ERROR: expected 24-bit BMP conversion block not found in src/TFTs.cpp")
    env.Exit(1)
