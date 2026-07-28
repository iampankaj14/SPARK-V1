import sys
from PIL import Image

def generate_frames():
    try:
        img_raw = Image.open('portal.png')
    except Exception as e:
        print("Error opening portal.png:", e)
        return
        
    NUM_FRAMES = 15
    SIZE = 140
    
    with open('portal_frames.c', 'w') as f:
        f.write("#include \"lvgl.h\"\n\n")
        
        for i in range(NUM_FRAMES):
            angle = i * (360 / NUM_FRAMES)
            
            background = Image.new("RGB", img_raw.size, (0, 0, 0))
            if img_raw.mode in ('RGBA', 'LA') or (img_raw.mode == 'P' and 'transparency' in img_raw.info):
                background.paste(img_raw, mask=img_raw.convert('RGBA').split()[3])
            else:
                background = img_raw.convert('RGB')
            
            rotated = background.rotate(-angle, resample=Image.Resampling.BICUBIC, expand=False, fillcolor=(0,0,0))
            resized = rotated.resize((SIZE, SIZE), Image.Resampling.LANCZOS)
            
            w, h = resized.size
            
            f.write(f"const uint8_t portal_frame_{i}_map[] = {{\n")
            pixels = resized.load()
            for y in range(h):
                f.write("    ")
                for x in range(w):
                    r, g, b = pixels[x, y]
                    b5 = (b >> 3) & 0x1F
                    g6 = (g >> 2) & 0x3F
                    r5 = (r >> 3) & 0x1F
                    c16 = (r5 << 11) | (g6 << 5) | b5
                    byte0 = (c16 >> 8) & 0xFF
                    byte1 = c16 & 0xFF
                    f.write(f"0x{byte0:02x}, 0x{byte1:02x}, ")
                f.write("\n")
            f.write("};\n\n")
            
            f.write(f"const lv_img_dsc_t portal_frame_{i} = {{\n")
            f.write("  .header.always_zero = 0,\n")
            f.write("  .header.w = {},\n".format(w))
            f.write("  .header.h = {},\n".format(h))
            f.write("  .data_size = {},\n".format(w * h * 2))
            f.write("  .header.cf = LV_IMG_CF_TRUE_COLOR,\n")
            f.write(f"  .data = portal_frame_{i}_map,\n")
            f.write("};\n\n")
            
        f.write("const lv_img_dsc_t* portal_frames[15] = {\n")
        for i in range(NUM_FRAMES):
            f.write(f"    &portal_frame_{i},\n")
        f.write("};\n")

if __name__ == '__main__':
    generate_frames()
