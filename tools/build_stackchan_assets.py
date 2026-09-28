"""Build opaque GIF pairs from the reviewed factory-style source on this Mac.

No serial access; outputs resources only. Keeps wake model and font byte-identical.
"""
import argparse, importlib.util, json, hashlib
from pathlib import Path
from PIL import Image

def module(path,name):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

def main():
    a=argparse.ArgumentParser();a.add_argument('--source',type=Path,required=True);a.add_argument('--output',type=Path,required=True);args=a.parse_args()
    root=args.output;root.mkdir(parents=True,exist_ok=True);(root/'gifs').mkdir(exist_ok=True)
    text=(args.source/'generate.py').read_text()
    text=text.replace("if name in ('neutral','sleepy'): mw=0","if name=='sleepy' or not TALK: mw=0")
    text=text.replace("if name in ('neutral','sleepy'): my=146","if name=='sleepy' or not TALK: my=146")
    ns={'__file__':str(args.source/'generate.py'),'__name__':'face_renderer','TALK':False}
    exec(compile(text,str(args.source/'generate.py'),'exec'),ns)
    packer=module(args.source.parent/'otto-20260929/prepare_assets.py','packer')
    old=packer.unpack((args.source/'assets.bin').read_bytes());index=json.loads(old['index.json'][0])
    old_emojis={x['file'] for x in index['emoji_collection']};files={k:v for k,v in old.items() if k not in old_emojis}
    collection=[];records=[]
    for emotion in ns['NAMES']:
        for talking in (False,True):
            if emotion=='sleepy' and talking:continue
            ns['TALK']=talking;key=emotion+('_talk' if talking else '');path=root/'gifs'/f'{key}.gif'
            ns['save_gif'](emotion,path)
            with Image.open(path) as im:
                mouth_shapes=set()
                for frame in range(im.n_frames):
                    im.seek(frame);im.load();assert im.size==(320,240)
                    # The central strip excludes tears, tongue and cheek decoration.
                    region=im.convert('L').crop((150,125,156,173)).point(lambda v:255 if v>160 else 0)
                    box=region.getbbox()
                    if box:mouth_shapes.add(box[3]-box[1])
                assert (max(mouth_shapes)-min(mouth_shapes)>=8) if talking else len(mouth_shapes)==1
            data=path.read_bytes();files[path.name]=(data,320,240);collection.append({'name':key,'file':path.name})
            records.append({'name':key,'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data)})
    index['emoji_collection']=collection;index['hide_subtitle']=True
    files['index.json']=(json.dumps(index,separators=(',',':')).encode(),0,0)
    binary=packer.pack(files);assert len(binary)<0x800000 and packer.unpack(binary)==files
    assert files[index['srmodels']]==old[index['srmodels']]
    (root/'assets.bin').write_bytes(binary)
    (root/'manifest.json').write_text(json.dumps({'gif_count':len(collection),'asset_bytes':len(binary),'sha256':hashlib.sha256(binary).hexdigest(),'files':records},indent=2))
    print(f'Validated {len(collection)} state-aware GIFs; assets {len(binary)} bytes')
if __name__=='__main__':main()
