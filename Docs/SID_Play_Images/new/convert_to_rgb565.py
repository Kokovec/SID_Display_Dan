import os
from PIL import Image
import numpy as np

def convert_to_rgb565_bmp(input_path, output_path):
    """
    Convert image to 320x240 RGB565 (16-bit R5G6B5) and save as proper BMP.
    """
    # Open and resize image
    with Image.open(input_path) as img:
        # Convert to RGB if it has alpha or other mode
        if img.mode != 'RGB':
            img = img.convert('RGB')
        
        # Resize to exactly 320x240 (using high quality resampling)
        img = img.resize((320, 240), Image.LANCZOS)
        
        # Convert to numpy array
        rgb = np.array(img, dtype=np.uint8)
        
        # Convert RGB888 -> RGB565
        r = (rgb[:, :, 0] >> 3).astype(np.uint16) << 11
        g = (rgb[:, :, 1] >> 2).astype(np.uint16) << 5
        b = (rgb[:, :, 2] >> 3).astype(np.uint16)
        rgb565 = r | g | b
        
        # Flatten to bytes (little-endian)
        data = rgb565.tobytes()
    
    # BMP dimensions
    width = 320
    height = 240
    bpp = 16
    row_size = ((width * bpp + 31) // 32) * 4   # BMP rows are padded to 4-byte boundary
    image_size = row_size * height
    
    # Create output directory if needed (already handled outside)
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    
    with open(output_path, 'wb') as f:
        # BITMAPFILEHEADER (14 bytes)
        f.write(b'BM')                                      # Signature
        f.write((54 + image_size).to_bytes(4, 'little'))    # File size
        f.write((0).to_bytes(4, 'little'))                  # Reserved
        f.write((54).to_bytes(4, 'little'))                 # Offset to pixel data
        
        # BITMAPINFOHEADER (40 bytes)
        f.write((40).to_bytes(4, 'little'))                 # Header size
        f.write(width.to_bytes(4, 'little'))                # Width
        f.write(height.to_bytes(4, 'little'))               # Height (positive = bottom-up)
        f.write((1).to_bytes(2, 'little'))                  # Planes
        f.write((16).to_bytes(2, 'little'))                 # Bits per pixel
        f.write((3).to_bytes(4, 'little'))                  # Compression = BI_BITFIELDS
        f.write(image_size.to_bytes(4, 'little'))           # Image size
        f.write((2835).to_bytes(4, 'little'))               # X pixels per meter (~72 dpi)
        f.write((2835).to_bytes(4, 'little'))               # Y pixels per meter
        f.write((0).to_bytes(4, 'little'))                  # Colors used
        f.write((0).to_bytes(4, 'little'))                  # Important colors
        
        # Bit masks for R5G6B5 (BI_BITFIELDS)
        f.write((0xF800).to_bytes(4, 'little'))             # Red mask   (11111000 00000000)
        f.write((0x07E0).to_bytes(4, 'little'))             # Green mask (00000111 11100000)
        f.write((0x001F).to_bytes(4, 'little'))             # Blue mask  (00000000 00011111)
        f.write((0).to_bytes(4, 'little'))                  # Alpha mask (none)
        
        # Write pixel data (BMP stores bottom-up, so we flip the rows)
        for y in range(height - 1, -1, -1):
            row = data[y * width * 2 : (y + 1) * width * 2]
            f.write(row)
            # Pad to 4-byte boundary if needed
            padding = row_size - (width * 2)
            if padding > 0:
                f.write(b'\x00' * padding)

# ============== MAIN SCRIPT ==============
if __name__ == "__main__":
    current_dir = os.getcwd()
    converted_dir = os.path.join(current_dir, "converted")
    
    # Create output folder
    os.makedirs(converted_dir, exist_ok=True)
    
    # Supported extensions
    extensions = {'.png', '.jpg', '.jpeg', '.PNG', '.JPG', '.JPEG'}
    
    converted_count = 0
    for filename in os.listdir(current_dir):
        if os.path.splitext(filename)[1] in extensions:
            input_path = os.path.join(current_dir, filename)
            base_name = os.path.splitext(filename)[0]
            output_path = os.path.join(converted_dir, base_name + ".bmp")
            
            try:
                convert_to_rgb565_bmp(input_path, output_path)
                print(f"✓ Converted: {filename} → {base_name}.bmp")
                converted_count += 1
            except Exception as e:
                print(f"✗ Failed {filename}: {e}")
    
    print(f"\nDone! {converted_count} file(s) converted to 'converted/' folder.")