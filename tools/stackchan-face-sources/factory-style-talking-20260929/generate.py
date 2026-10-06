"""Render code-native StackChan-style geometry; never accesses a serial device."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import math, json, hashlib, zipfile

ROOT = Path(__file__).resolve().parent
S = 4
W, H = 320, 240
NAMES = 'neutral happy laughing funny sad angry crying loving embarrassed surprised shocked thinking winking cool relaxed delicious kissy confident sleepy silly confused'.split()
LABELS = ['默认','开心','大笑','逗趣','难过','生气','哭泣','喜欢','害羞','惊讶','震惊','思考','眨眼','酷酷','放松','美味','亲亲','自信','困困','调皮','疑惑']
BASE = {'neutral':(100,0),'happy':(72,155),'angry':(70,45),'sad':(70,-40),'sleepy':(35,-5),'confused':(75,0)}
STYLES = {'laughing':'happy','funny':'happy','crying':'sad','loving':'happy','embarrassed':'happy','relaxed':'sleepy','delicious':'happy','kissy':'happy','confident':'happy','silly':'happy'}

def smooth(x):
    x=max(0,min(1,x)); return x*x*(3-2*x)

def eye(im,cx,cy,weight,angle,diam=20):
    tile=Image.new('RGB',(40*S,40*S),'black'); d=ImageDraw.Draw(tile)
    lo=(40-diam)*S/2; hi=(40+diam)*S/2
    d.ellipse((lo,lo,hi-1,hi-1),fill='white')
    cutoff=(20+diam/2-diam*weight/100)*S
    if weight<100: d.rectangle((lo,0,hi,cutoff),fill='black')
    tile=tile.rotate(-angle,resample=Image.Resampling.BICUBIC)
    im.paste(tile,(round((cx-20)*S),round((cy-20)*S)))

def render(name,t,*,talking=False):
    im=Image.new('RGB',(W*S,H*S),'black'); d=ImageDraw.Draw(im)
    def line(points,fill='white',width=2): d.line([(int(x*S),int(y*S)) for x,y in points],fill=fill,width=max(1,round(width*S)),joint='curve')
    def ellipse(box,fill): d.ellipse(tuple(round(v*S) for v in box),fill=fill)
    def heart(cx,cy,r):
        pts=[]
        for j in range(81):
            a=2*math.pi*j/80
            pts.append((cx+r*math.sin(a)**3,cy-r*(13*math.cos(a)-5*math.cos(2*a)-2*math.cos(3*a)-math.cos(4*a))/16))
        d.polygon([(round(x*S),round(y*S)) for x,y in pts],fill='#E13232')
    phase=2*math.pi*t
    gx=2.7*math.sin(phase); gy=1.2*math.sin(phase*2)
    weight,angle=BASE.get(STYLES.get(name,name),(100,0))
    # One 240 ms blink in a 6-second loop. Keep the loop seam continuous.
    blink=max(0,1-abs(t-.80)/.025)
    lw=rw=weight*(1-blink)+25*blink
    if name=='winking': lw=100-75*(.5-.5*math.cos(phase))
    if name=='thinking': gx=3; gy=-2; lw=75; rw=100; angle=-10
    if name=='confused': lw=68+12*math.sin(phase); rw=92; gx=2*math.sin(phase)
    if name=='cool': lw=rw=55; angle=0
    if name=='confident': lw=85; rw=66; angle=15
    diam=24 if name=='shocked' else 22 if name=='surprised' else 20
    eye(im,90+gx,104+gy,lw,angle,diam)
    eye(im,230+gx,104+gy,rw,-angle,diam)
    # Talking articulates every non-sleepy emotion, retaining its eye expression.
    # Idle states always keep a closed mouth. The envelope pauses at the seam.
    syllable=.5-.5*math.cos(phase*14)
    envelope=.35+.65*(.5-.5*math.cos(phase*3))
    amplitude=48 if name in ('sleepy','relaxed','sad') else 72
    mw=3+amplitude*syllable*envelope
    if name=='sleepy' or not talking: mw=0
    width=90-.30*mw; height=6+.44*mw; radius=.16*mw
    my=146+.9*math.sin(phase)
    if name=='sleepy' or not talking: my=146
    # The mouth uses the official width/height/radius mapping.
    if name=='kissy': width=26+mw*.10; height=6+mw*.34; radius=5+mw*.06
    d.rounded_rectangle(((160-width/2)*S,(my-height/2)*S,(160+width/2)*S,(my+height/2)*S),radius=radius*S,fill='white')
    if name=='silly': d.rounded_rectangle((157*S,148*S,171*S,164*S),radius=6*S,fill='#E88A96')
    if name in ('embarrassed','loving'):
        for x in (75,235):
            for k in range(3): line([(x+k*5,124),(x+3+k*5,131)],'#E88A96',1.4)
    if name in ('loving','kissy'): heart(268,51+2*math.sin(phase),10+math.sin(phase))
    if name=='crying':
        for x,shift in ((90,0),(230,.5)):
            drop=(t*2+shift)%1; y=118+drop*36
            ellipse((x-2,y,x+2,y+7),'#79BEEB')
    if name=='angry':
        x,y=263,66
        for sx,sy in ((-1,-1),(1,-1),(-1,1),(1,1)):
            line([(x+sx*3,y+sy*10),(x+sx*3,y+sy*3),(x+sx*10,y+sy*3)],'#E13232',2)
    if name=='thinking' or name=='confused':
        # Small hand-drawn question mark, avoiding platform-dependent fonts.
        line([(262,62),(263,59),(267,58),(271,60),(271,64),(267,67),(267,70)],'white',1.7)
        ellipse((266,74,268,76),'white')
    if name=='sleepy':
        for k in range(2):
            x=256+k*13; y=69-k*12+2*math.sin(phase)
            line([(x,y),(x+7,y),(x,y+8),(x+7,y+8)],'white',1.3)
    return im.resize((W,H),Image.Resampling.LANCZOS)

# Fixed palette: grayscale antialiasing plus accent ramps; opaque black background.
palette=[]
for v in range(128): palette.extend([round(v*255/127)]*3)
for color in ((225,50,50),(232,138,150),(121,190,235)):
    for v in range(40): palette.extend([round(c*v/39) for c in color])
palette += [0]*(768-len(palette))
pal=Image.new('P',(1,1)); pal.putpalette(palette)

def save_gif(name,dest,*,talking=False):
    frames=[render(name,j/75,talking=talking).quantize(palette=pal,dither=Image.Dither.NONE) for j in range(75)]
    frames[0].save(dest,save_all=True,append_images=frames[1:],duration=80,loop=0,optimize=False,disposal=1)

def main():
    (ROOT/'gifs').mkdir(exist_ok=True); (ROOT/'demos').mkdir(exist_ok=True)
    for name in NAMES: save_gif(name,ROOT/'gifs'/f'{name}.gif',talking=name not in ('neutral','sleepy'))
    save_gif('neutral',ROOT/'demos'/'speaking.gif',talking=True)
    font=ImageFont.load_default(size=14)
    sheet=Image.new('RGB',(320*4,270*6),'#202024'); sd=ImageDraw.Draw(sheet)
    for i,(name,label) in enumerate(zip(NAMES,LABELS)):
        x=(i%4)*320; y=(i//4)*270
        sheet.paste(render(name,.4,talking=name not in ('neutral','sleepy')),(x,y)); sd.text((x+12,y+246),name,font=font,fill='white')
    sheet.save(ROOT/'contact-sheet.png')
    entries=[]
    for path in sorted((ROOT/'gifs').glob('*.gif'))+list((ROOT/'demos').glob('*.gif')):
        with Image.open(path) as gif:
            assert gif.size==(320,240) and gif.info.get('loop')==0
            assert 'transparency' not in gif.info
            duration=0; hashes=set(); mouth_heights=set()
            for j in range(gif.n_frames):
                gif.seek(j); rgb=gif.convert('RGB'); duration+=gif.info['duration']
                assert all(rgb.getpixel(p)==(0,0,0) for p in ((0,0),(319,0),(0,239),(319,239)))
                hashes.add(hashlib.sha256(rgb.tobytes()).hexdigest())
                mouth=rgb.crop((150,125,156,173)).convert('L').point(lambda v: 255 if v>160 else 0)
                bounds=mouth.getbbox()
                if bounds: mouth_heights.add(bounds[3]-bounds[1])
            assert len(hashes)>1 and duration==6000
            if path.stem in ('neutral','sleepy'):
                assert len(mouth_heights)==1, 'Idle and sleepy mouths must remain still'
            else:
                assert max(mouth_heights)-min(mouth_heights)>=8, f'Mouth does not articulate: {path.name}'
            entries.append({'file':str(path.relative_to(ROOT)),'frames':gif.n_frames,'duration_ms':duration,'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
    (ROOT/'manifest.json').write_text(json.dumps({'source_commit':'1b5765599fba8aaad1811d9a79358ccc7051f5f3','size':[320,240],'emotion_count':21,'demo_count':1,'files':entries},ensure_ascii=False,indent=2))
    cards=''.join(f'<figure><img src="gifs/{n}.gif" width="320" height="240"><figcaption>{l} · {n}</figcaption></figure>' for n,l in zip(NAMES,LABELS))
    (ROOT/'preview.html').write_text('<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>StackChan 原厂风格 GIF</title><style>body{background:#161619;color:#eee;font:16px system-ui;margin:32px}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:18px}figure{margin:0;background:#000;border-radius:12px;overflow:hidden}img{display:block;margin:auto}figcaption{padding:12px;color:#ccc}p{color:#bbb;line-height:1.7}</style><h1>StackChan 原厂风格 GIF</h1><p>依据官方几何比例重绘 · 320 × 240 · 黑色背景 · 21 个表情 · 默认与困困闭嘴，其余动嘴<br>除默认与困困保持闭嘴外，其余嘴巴持续循环开合，不随实际语音启停。<br>这是风格复刻，不是从原厂 1.5.1 固件导出的原始素材。未写入机器人。</p><main>'+cards+'</main><h2>说话动作演示</h2><p>独立动画演示，播放节奏不随实际语音变化。</p><img src="demos/speaking.gif" width="320" height="240"></html>')
    with zipfile.ZipFile(ROOT/'stackchan-factory-style-gifs.zip','w',zipfile.ZIP_DEFLATED) as z:
        for p in sorted(ROOT.rglob('*')):
            rel=p.relative_to(ROOT)
            if p.is_file() and (rel.parts[0] in ('gifs','demos','source') or str(rel) in ('generate.py','README.md','manifest.json','preview.html','contact-sheet.png')):
                z.write(p,rel)
    print(json.dumps({'verified_files':len(entries),'total_gif_bytes':sum(e['bytes'] for e in entries),'zip':str(ROOT/'stackchan-factory-style-gifs.zip')},ensure_ascii=False))

if __name__=='__main__': main()
