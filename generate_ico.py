#!/usr/bin/env python3
"""
Generate phraims.ico for Windows from existing PNG files in phraims.iconset.
Windows .ico files should contain multiple resolutions: 16, 32, 48, 64, 128, 256.
"""

from PIL import Image
import os
import sys

def main():
    # Source and destination paths
    iconset_dir = "phraims.iconset"
    output_ico = "resources/phraims.ico"
    
    # Check if iconset directory exists
    if not os.path.isdir(iconset_dir):
        print(f"Error: {iconset_dir} directory not found", file=sys.stderr)
        sys.exit(1)
    
    # PNG files to include in the .ico (standard Windows sizes)
    # Windows typically uses 16, 32, 48, 64, 128, 256
    png_files = [
        "icon_16x16.png",       # 16x16
        "icon_32x32.png",       # 32x32
        "icon_32x32@2x.png",    # 64x64
        "icon_128x128.png",     # 128x128
        "icon_256x256.png",     # 256x256
    ]
    
    # Also need 48x48 which doesn't exist, so we'll generate it from 256
    images = []
    
    for png_file in png_files:
        png_path = os.path.join(iconset_dir, png_file)
        if os.path.exists(png_path):
            img = Image.open(png_path)
            images.append(img)
            print(f"Added {png_file} ({img.size[0]}x{img.size[1]})")
        else:
            print(f"Warning: {png_file} not found", file=sys.stderr)
    
    # Generate 48x48 from 256x256
    icon_256_path = os.path.join(iconset_dir, "icon_256x256.png")
    if os.path.exists(icon_256_path):
        img_256 = Image.open(icon_256_path)
        # Use LANCZOS resampling for high-quality downscaling
        # Handle both new (Pillow >= 10.0) and old (< 10.0) API
        try:
            resample = Image.Resampling.LANCZOS
        except AttributeError:
            resample = Image.LANCZOS
        img_48 = img_256.resize((48, 48), resample)
        images.insert(2, img_48)  # Insert after 32x32
        print(f"Generated 48x48 from 256x256")
    
    if not images:
        print("Error: No PNG images found to convert", file=sys.stderr)
        sys.exit(1)
    
    # Ensure resources directory exists
    os.makedirs("resources", exist_ok=True)
    
    # Save as .ico with multiple resolutions
    # PIL/Pillow requires saving each size explicitly
    sizes = [(img.size[0], img.size[1]) for img in images]
    
    # The primary image should be the largest for best quality
    # Reorder with largest first
    images_sorted = sorted(images, key=lambda img: img.size[0] * img.size[1], reverse=True)
    
    images_sorted[0].save(
        output_ico,
        format='ICO',
        sizes=sizes,
        append_images=images_sorted[1:] if len(images_sorted) > 1 else None
    )
    
    # Verify the file was created
    if os.path.exists(output_ico):
        size_bytes = os.path.getsize(output_ico)
        print(f"\nSuccessfully created {output_ico}")
        print(f"Size: {size_bytes} bytes ({size_bytes // 1024} KB)")
    else:
        print(f"Error: Failed to create {output_ico}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
