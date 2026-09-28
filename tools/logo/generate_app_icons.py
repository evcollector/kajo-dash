"""Generate KAJO app icons from shared vector geometry. Requires Pillow."""
from pathlib import Path
import math
from PIL import Image, ImageDraw
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/"tools/logo"
ORANGE="#ff7900"
BOLT=[(59,25),(38,57),(49,57),(45,80),(70,43),(56,43)]
def raw_arc_points(r):
    return [(54+r*math.cos(math.radians(a)),51+r*math.sin(math.radians(a))) for a in range(135,406)]
# Center the visible silhouette, not the full circle: its open bottom makes
# the original circle center sit below the artwork's bounding-box center.
outline = raw_arc_points(43) + BOLT
OFFSET_X = 54 - (min(x for x,y in outline) + max(x for x,y in outline))/2
OFFSET_Y = 54 - (min(y for x,y in outline) + max(y for x,y in outline))/2
BOLT = [(x+OFFSET_X,y+OFFSET_Y) for x,y in BOLT]
def arc_points(r):
    return [(x+OFFSET_X,y+OFFSET_Y) for x,y in raw_arc_points(r)]

def ring_path(outer,inner):
    a,b=arc_points(outer)[0],arc_points(outer)[-1]
    c,d=arc_points(inner)[-1],arc_points(inner)[0]
    return f"M{a[0]:.4f},{a[1]:.4f} A{outer},{outer} 0 1 1 {b[0]:.4f},{b[1]:.4f} L{c[0]:.4f},{c[1]:.4f} A{inner},{inner} 0 1 0 {d[0]:.4f},{d[1]:.4f} Z"
PATHS=[(ORANGE,ring_path(43,41.5)),(ORANGE,ring_path(36,28)),("#ffffff","M"+" L".join(f"{x},{y}" for x,y in BOLT)+" Z")]
def write(path,text):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(text,encoding="utf-8")
def vector(adaptive=False,mono=False):
    paths="\n".join(f'    <path android:fillColor="{("#ffffff" if mono else c)}" android:pathData="{d}" />' for c,d in PATHS)
    group=(f'<group android:pivotX="54" android:pivotY="54" android:scaleX="0.72" android:scaleY="0.72">{paths}</group>' if adaptive else '<path android:fillColor="#000000" android:pathData="M0,0H108V108H0Z" />'+paths)
    return '<?xml version="1.0" encoding="utf-8"?>\n<vector xmlns:android="http://schemas.android.com/apk/res/android" android:width="108dp" android:height="108dp" android:viewportWidth="108" android:viewportHeight="108">\n'+group+'\n</vector>\n'
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    write(OUT/"kajo_app_icon.svg",'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 108 108"><rect width="108" height="108" rx="22" fill="#000000"/>'+''.join(f'<path fill="{c}" d="{d}"/>' for c,d in PATHS)+'</svg>\n')
    scale=12
    image=Image.new("RGBA",(108*scale,108*scale))
    draw=ImageDraw.Draw(image)
    draw.rounded_rectangle((0,0,108*scale-1,108*scale-1),radius=22*scale,fill="#000000")
    def polygon(points,color): draw.polygon([(round(x*scale),round(y*scale)) for x,y in points],fill=color)
    for outer,inner in [(43,41.5),(36,28)]:
        polygon(arc_points(outer)+list(reversed(arc_points(inner))),ORANGE)
    polygon(BOLT,"#ffffff")
    icon=image.resize((512,512),Image.Resampling.LANCZOS)
    icon.save(OUT/"kajo_app_icon.png")
    icon.save(OUT/"kajo_app_icon.ico",sizes=[(s,s) for s in [16,24,32,48,64,128,256]])
    (ROOT/"BrowserWrapper/AppIcon.ico").write_bytes((OUT/"kajo_app_icon.ico").read_bytes())
    # The companion app is a separate private checkout; public clones lack it.
    res=ROOT/"android-companion/app/src/main/res"
    if not res.is_dir():
        print("Generated SVG, PNG and multi-size ICO. No android-companion checkout; Android icons skipped.")
        return
    write(res/"drawable/ic_launcher.xml",vector())
    write(res/"drawable/ic_launcher_foreground.xml",vector(adaptive=True))
    write(res/"drawable/ic_launcher_monochrome.xml",vector(adaptive=True,mono=True))
    write(res/"values/icon_colors.xml",'<?xml version="1.0" encoding="utf-8"?>\n<resources><color name="ic_launcher_background">#000000</color></resources>\n')
    for directory,mono in [("mipmap-anydpi-v26",False),("mipmap-anydpi-v33",True)]:
        write(res/directory/"ic_launcher.xml",'<?xml version="1.0" encoding="utf-8"?>\n<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n  <background android:drawable="@color/ic_launcher_background" />\n  <foreground android:drawable="@drawable/ic_launcher_foreground" />\n'+('  <monochrome android:drawable="@drawable/ic_launcher_monochrome" />\n' if mono else '')+'</adaptive-icon>\n')
    print("Generated SVG, PNG, multi-size ICO, Android adaptive and themed icons.")
if __name__=="__main__": main()
