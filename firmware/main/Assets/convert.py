import sys
from PIL import Image

def convert():
    img = Image.open('portal.png')
    
    # Create a black background image
    background = Image.new("RGB", img.size, (0, 0, 0))
    if img.mode in ('RGBA', 'LA') or (img.mode == 'P' and 'transparency' in img.info):
        background.paste(img, mask=img.convert('RGBA').split()[3]) # paste using alpha channel as mask
    else:
        background = img.convert('RGB')
        
    img = background
    
    # Resize to 100x100
    img = img.resize((100, 100), Image.Resampling.LANCZOS)
    
    w, h = img.size
    
    with open('portal_image.c', 'w') as f:
        f.write("#include \"lvgl.h\"\n\n")
        f.write("const uint8_t portal_image_map[] = {\n")
        
        pixels = img.load()
        for y in range(h):
            f.write("    ")
            for x in range(w):
                r, g, b = pixels[x, y]
                
                b5 = (b >> 3) & 0x1F
                g6 = (g >> 2) & 0x3F
                r5 = (r >> 3) & 0x1F
                
                c16 = (r5 << 11) | (g6 << 5) | b5
                
                # CONFIG_LV_COLOR_16_SWAP=y
                byte0 = (c16 >> 8) & 0xFF
                byte1 = c16 & 0xFF
                
                f.write(f"0x{byte0:02x}, 0x{byte1:02x}, ")
            f.write("\n")
            
        f.write("};\n\n")
        
        f.write("const lv_img_dsc_t portal_image = {\n")
        f.write("  .header.always_zero = 0,\n")
        f.write("  .header.w = {},\n".format(w))
        f.write("  .header.h = {},\n".format(h))
        f.write("  .data_size = {},\n".format(w * h * 2))
        f.write("  .header.cf = LV_IMG_CF_TRUE_COLOR,\n")
        f.write("  .data = portal_image_map,\n")
        f.write("};\n")

if __name__ == '__main__':
    convert()
