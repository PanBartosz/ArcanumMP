#!/usr/bin/env python3
"""Reproducible 1080p editorial assembly of actual native gameplay takes.

The edit decision list records source ranges and crops. Captions, graphics and
sound design are editorial layers; gameplay is never AI-generated or retimed.
"""
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import math
import os
from pathlib import Path
import subprocess
import textwrap

import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT=Path(__file__).resolve().parents[2]
WORK=Path(os.environ.get('ARCMP_SHOWCASE_WORK',str(ROOT/'evidence/showcase')))
TAKES=ROOT/'evidence/showcase/takes'
ART=WORK/'graphics'
CUTS=WORK/'edit'
DELIVERY=Path(os.environ.get('ARCMP_SHOWCASE_DELIVERY',str(ROOT/'dist/showcase')))
W,H,FPS=1920,1080,30
GOLD=(202,170,110,255)
IVORY=(237,230,211,255)
MUTED=(164,160,148,255)
SERIF='/usr/share/fonts/gnu-free/FreeSerif.otf'
BOLD='/usr/share/fonts/gnu-free/FreeSerifBold.otf'
SANS='/usr/share/fonts/liberation/LiberationSans-Regular.ttf'
SANS_BOLD='/usr/share/fonts/liberation/LiberationSans-Bold.ttf'


def font(size,bold=False,sans=False):
    return ImageFont.truetype((SANS_BOLD if bold else SANS) if sans else (BOLD if bold else SERIF),size)


def tracked(draw,text,y,size=24,spacing=6,color=GOLD,x=None):
    f=font(size,sans=True)
    width=sum(draw.textlength(c,font=f) for c in text)+spacing*(len(text)-1)
    pos=(W-width)/2 if x is None else x
    for c in text:
        draw.text((pos,y),c,font=f,fill=color)
        pos+=draw.textlength(c,font=f)+spacing


def background():
    rng=np.random.default_rng(20261030)
    y,x=np.mgrid[0:H,0:W]
    glow=np.clip(1-np.sqrt(((x-W*.53)/1400)**2+((y-H*.42)/950)**2),0,1)
    grain=rng.normal(0,.75,(H,W))
    data=np.stack([12+10*glow+grain,13+9*glow+grain,13+6*glow+grain],axis=2).clip(0,255).astype(np.uint8)
    im=Image.fromarray(data,'RGB').convert('RGBA')
    gears=Image.new('RGBA',(W,H));d=ImageDraw.Draw(gears)
    for cx,cy,r in [(1740,860,310),(1450,1110,225),(-40,210,260)]:
        for rr in [r,r-15,r*.75,r*.65,r*.2]:
            d.ellipse((cx-rr,cy-rr,cx+rr,cy+rr),outline=(139,119,76,22),width=2)
        for j in range(24):
            a=2*math.pi*j/24
            pts=[(cx+math.cos(a+da)*rad,cy+math.sin(a+da)*rad) for rad,da in [(r-14,-.035),(r+13,-.035),(r+13,.035),(r-14,.035)]]
            d.line(pts+[pts[0]],fill=(139,119,76,24),width=3)
        for j in range(8):
            a=math.pi*j/4
            d.line((cx+math.cos(a)*r*.23,cy+math.sin(a)*r*.23,cx+math.cos(a)*r*.63,cy+math.sin(a)*r*.63),fill=(139,119,76,18),width=10)
    im.alpha_composite(gears)
    d=ImageDraw.Draw(im)
    d.rectangle((26,26,W-27,H-27),outline=(116,96,57,110),width=1)
    for x,y,dx,dy in [(42,42,1,1),(W-43,42,-1,1),(42,H-43,1,-1),(W-43,H-43,-1,-1)]:
        d.line((x,y,x+dx*48,y),fill=(170,140,83,190),width=2)
        d.line((x,y,x,y+dy*48),fill=(170,140,83,190),width=2)
    im.convert('RGB').save(ART/'background.png')


def metallic_word(im,text,y,size=156,spacing=6):
    mask=Image.new('L',(W,H));d=ImageDraw.Draw(mask);f=font(size,True)
    widths=[d.textlength(c,font=f) for c in text]
    x=(W-sum(widths)-spacing*(len(text)-1))/2
    for c,width in zip(text,widths):
        d.text((x,y),c,font=f,fill=255,stroke_width=1);x+=width+spacing
    shadow=Image.new('RGBA',(W,H),(0,0,0,0))
    shadow.paste((0,0,0,230),(0,5),mask)
    im.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(6)))
    yy=np.arange(H)[:,None]
    k=np.clip((yy-y)/(size*1.1),0,1)
    # Muted brass with a narrow engraved highlight, matching the native HUD.
    cols=np.stack([229-111*k,208-124*k,158-112*k],axis=-1).clip(0,255).astype(np.uint8)
    color=Image.fromarray(np.repeat(cols,W,axis=1),'RGB').convert('RGBA')
    color.putalpha(mask);im.alpha_composite(color)


def title_card(end=False):
    im=Image.open(ART/'background.png').convert('RGBA');d=ImageDraw.Draw(im)
    tracked(d,'THE ORIGINAL CAMPAIGN',238,22,7)
    metallic_word(im,'ARCANUM',310)
    d=ImageDraw.Draw(im)
    tracked(d,'M U L T I P L A Y E R',502,34,6,IVORY)
    d.line((520,579,912,579),fill=GOLD,width=1)
    d.line((1008,579,1400,579),fill=GOLD,width=1)
    d.polygon([(960,568),(971,579),(960,590),(949,579)],outline=GOLD)
    line='GET YOUR PARTY TOGETHER.' if end else 'AN OLD WORLD. NEW COMPANY.'
    tracked(d,line,632,25,4,IVORY)
    tracked(d,'4-PLAYER CAMPAIGN CO-OP  ·  WINDOWS + LINUX',714,20,3,MUTED)
    if end:
        tracked(d,'panbartosz.github.io/ArcanumMP',879,20,2,IVORY)
        tracked(d,'Free community mod. Original game required. Staged encounters shown.',928,14,1,MUTED)
        tracked(d,'Music: Arcadia & Five Armies — Kevin MacLeod / incompetech.com / CC BY 4.0',969,13,1,MUTED)
    else:
        tracked(d,'ArcanumMP',913,19,3,MUTED)
    p=ART/('end.png' if end else 'title.png');im.convert('RGB').save(p);return p


def label(name,title,subtitle='',mode='cinema',body=()):
    im=Image.new('RGBA',(W,H));d=ImageDraw.Draw(im)
    if mode=='cinema':
        d.rectangle((0,0,W,104),fill=(6,7,8,255));d.rectangle((0,960,W,H),fill=(6,7,8,255))
        d.line((68,101,W-68,101),fill=(139,113,66,150),width=1)
        tracked(d,title,34,25,4,IVORY)
        if subtitle:tracked(d,subtitle,1005,19,2,MUTED)
    elif mode=='feature':
        tracked(d,'MAKE IT YOURS',159,17,4,GOLD,x=76)
        y=222
        for line in textwrap.wrap(title,15):
            d.text((74,y),line,font=font(52,True),fill=IVORY);y+=61
        d.line((77,y+24,340,y+24),fill=GOLD,width=2);y+=67
        for sentence in body:
            for line in textwrap.wrap(sentence,26):
                d.text((78,y),line,font=font(27,sans=True),fill=MUTED);y+=39
            y+=23
        d.text((76,995),'ARCANUM MP',font=font(19,sans=True),fill=GOLD)
        if subtitle:d.text((645,1020),subtitle,font=font(21,sans=True),fill=MUTED)
    elif mode=='wide':
        d.rectangle((0,0,W,105),fill=(6,7,8,242))
        d.text((66,27),title,font=font(42,True),fill=IVORY)
        if subtitle:
            d.rectangle((0,997,W,H),fill=(6,7,8,245));d.text((66,1018),subtitle,font=font(24,sans=True),fill=MUTED)
    elif mode=='split':
        tracked(d,title,48,28,5,IVORY)
        if subtitle:tracked(d,subtitle,92,16,2,MUTED)
        for i,(x,y) in enumerate([(104,126),(1000,126),(104,604),(1000,604)]):
            d.rectangle((x-2,y-2,x+818,y+461),outline=(161,132,79,240),width=2)
            d.rectangle((x,y+423,x+816,y+459),fill=(8,9,10,235))
            d.text((x+14,y+430),['HOST · SILAS','PLAYER 2 · MORGAN','PLAYER 3 · ALARIC','PLAYER 4 · LUCAN'][i],font=font(19,sans=True),fill=IVORY)
    p=ART/(name+'.png');im.save(p);return p


def source(label,index=0):return TAKES/label/f'peer-{index}.mp4'


def shot(label,name,offset=0):
    p=TAKES/label
    meta=json.loads((p/'capture.json').read_text())['peers'][0]
    scenes=json.loads((p/'shots.json').read_text())
    scene=next(s for s in scenes if s['name']==name)
    return scene['frame']/scene.get('fps',30)+offset if 'frame' in scene else scene['monotonic']-meta['started_monotonic']+offset


def make_plan():
    plan=[]
    def add(name,label,start,duration,title,subtitle='',mode='cinema',index=0,crop=None,body=()):
        plan.append(dict(name=name,source=str(source(label,index)),start=start,duration=duration,title=title,subtitle=subtitle,mode=mode,crop=crop,body=body))
    def ui(name,label,scene,duration,title,body=(),offset=0,mode='feature',subtitle='',crop=(260,75,760,570)):
        add(name,label,shot(label,scene,offset),duration,title,subtitle,mode,crop=crop,body=body)
    mp='launch-v3';tools='ui-live-v3';details='details-live-v3';world='world-polished-v3'
    # Every changed HUD is recaptured. Source times refer to completed native frames.
    battle=shot(mp,'tulla-battle')
    add('01-cold-open',mp,battle+1.8,3.5,'MAGICK IS BETTER WITH COMPANY.','Four players. One campaign.',crop=(160,115,980,390))
    ui('02-world',world,'pan-Qintarra',3.3,'A WORLD WORTH RETURNING TO.',mode='cinema',crop=(140,70,1060,460))
    plan.append(dict(name='03-title',mode='card',image=str(ART/'title.png'),duration=4.0))
    plan.append(dict(name='04-four-players',mode='split',label=mp,scene='independent-paths',start=.8,duration=5.5,title='FOUR PLAYERS. ONE CAMPAIGN.',subtitle='Your own character. Your own path. The same world.'))
    add('05-steel',mp,battle+4.5,3.7,'STEEL.','Melee, ranged combat and native enemy AI.',crop=(160,110,980,400))
    add('06-gunpowder',mp,shot(mp,'tulla-battle')+6,3.3,'GUNPOWDER.','Bring your own approach.',index=2,crop=(160,110,980,400))
    add('07-fireflash',mp,battle+10.8,3.4,'AND MAGICK.','Fireflash',crop=(160,110,980,400))
    add('08-tulla',mp,battle+1.0,3.0,'STAND TOGETHER.','At the gates of Tulla.',index=1,crop=(160,110,980,400))
    ui('09-turns',mp,'turn-order-and-log',4.0,'SEE THE NEXT MOVE.',mode='wide',subtitle='Visible initiative · Current actor · Remaining AP',crop=None)
    ui('10-portrait-target',mp,'turn-order-and-log',3.0,'THE FIGHT, AT A GLANCE.',mode='wide',subtitle='Clickable portraits · Conditions · Shared combat history',crop=None,offset=3.5)
    add('11-party',mp,battle+4,3.3,'Your party, in view.',mode='feature',crop=(0,42,89,390),body=('Health and fatigue.','Conditions on portraits.','Select an ability. Click a portrait to target.'))
    ui('12-log',mp,'turn-order-and-log',3.7,'A shared combat log',crop=(846,266,422,200),body=('Read hits, misses and effects.','Follow the same fight together.'))
    ui('13-drag',tools,'combat-log-drag',3.1,'Put it where you want it.',mode='wide',subtitle='Live movement. Native controls.',crop=None)
    ui('14-resize',tools,'combat-log-resize',3.1,'Give it room.',mode='wide',subtitle='Resize the window while you play.',crop=None)
    ui('15-alpha',tools,'combat-log-transparency',3.7,'Your view. Your UI.',mode='wide',subtitle='Individual transparency · Your layout, remembered',crop=None)
    ui('16-log-tools',tools,'combat-log-expanded',3.7,'Read the details.',('Expand the log.','Filter events. Export the history.'),crop=(250,75,780,570))
    ui('17-actions',tools,'actions-search',4.2,'Find your next move.',('Search spells and skills.','Read the native descriptions.'))
    ui('18-inventory',details,'inventory-search',5.5,'Less inventory friction.',('Search and filter.','Compare equipped gear.','Mark junk for selling.'))
    ui('19-crafting',details,'crafting-learned-recipes',4.2,'Know what you can craft.',('Craftable recipes.','Ingredients and requirements.'))
    ui('20-quests',tools,'quests-note-and-pin',4.0,'Keep your own trail.',('Pin discovered objectives.','Add a personal next step.'))
    ui('21-text',tools,'settings-text-125',4.8,'Make it readable.',('Independent text scaling.','Contrast and hover help.'),crop=(250,75,780,570))
    ui('22-keys',tools,'settings-remapping',3.0,'Make it familiar.',('Remap commands.','Choose auto-pause triggers.'),crop=(250,75,780,570))
    ui('23-action-rows',tools,'hud-drag-action-rows',3.1,'A HUD that fits your play.',mode='wide',subtitle='Move and resize the extra action rows.',crop=None)
    ui('24-quickslots',details,'quickslots-all-icons',2.8,'YOUR SPELLS. YOUR SKILLS. YOUR ITEMS.','Assignable slots with original game icons',mode='cinema',crop=(90,440,1100,120))
    ui('25-options','options-live-v3','display-options',4.0,'Modern display controls.',('Widescreen or classic.','UI size. Walk or run.','Personal settings, saved.'),crop=(480,160,530,390))
    ui('26-zoom',tools,'zoom-tarant',3.3,'ZOOM TO THE MOMENT.','Mouse-wheel zoom · Fixed-size UI',mode='wide',crop=None)
    ui('27-tarant',world,'pan-Tarant',2.6,'TARANT',mode='cinema',crop=(100,70,1150,460))
    ui('28-qintarra',world,'pan-Qintarra',2.6,'QINTARRA',mode='cinema',crop=(100,70,1150,460))
    ui('29-tulla',world,'pan-Tulla',2.6,'TULLA',mode='cinema',crop=(100,70,1150,460))
    ui('30-ruins',world,'pan-Vendigroth Ruins',2.6,'AND BEYOND.',mode='cinema',crop=(100,70,1150,460))
    add('31-closing-fight',mp,battle+11.0,3.2,'THE ORIGINAL CAMPAIGN. TOGETHER.','Dialogue · Travel · Shared saves · Cross-play',crop=(160,110,980,400))
    plan.append(dict(name='32-end',mode='card',image=str(ART/'end.png'),duration=7.0))
    return plan


def render_segment(item):
    output=CUTS/(item['name']+'.mp4')
    stamp=CUTS/(item['name']+'.json')
    signature=json.dumps(item,sort_keys=True)
    if output.exists() and stamp.exists() and stamp.read_text()==signature:
        return output
    args=['ffmpeg','-nostdin','-hide_banner','-loglevel','error','-y','-filter_complex_threads','1']
    duration=item['duration'];mode=item['mode']
    if mode=='card':
        args+=['-loop','1','-framerate','30','-i',item['image']]
        chain=f'[0:v]scale={W}:{H},setsar=1,fade=t=in:st=0:d=0.35,fade=t=out:st={duration-.4}:d=0.4[v]'
    elif mode=='split':
        p=TAKES/item['label'];meta=json.loads((p/'capture.json').read_text())['peers']
        base=meta[0]['started_monotonic']
        for i in range(4):
            if item.get('scene'):
                entry=next(s for s in json.loads((p/f'actions-{i}.json').read_text()) if s['name']==item['scene'])
                start=entry['frame']/30+item['start']
            else:start=item['start']+base-meta[i]['started_monotonic']
            args+=['-ss',str(start),'-i',str(source(item['label'],i))]
        overlay=label(item['name'],item['title'],item['subtitle'],'split')
        args+=['-loop','1','-i',str(ART/'background.png'),'-loop','1','-i',str(overlay)]
        chain=''
        for i in range(4):chain+=f'[{i}:v]fps=30,scale=816:459:flags=lanczos,setsar=1,setpts=PTS-STARTPTS[g{i}];'
        chain+='[4:v]format=rgba[b];'
        prior='b'
        for i,(x,y) in enumerate([(104,126),(1000,126),(104,604),(1000,604)]):
            chain+=f'[{prior}][g{i}]overlay={x}:{y}:shortest=1[c{i}];';prior=f'c{i}'
        chain+=f'[{prior}][5:v]overlay=0:0:shortest=1,fade=t=in:st=0:d=0.15,fade=t=out:st={duration-.15}:d=0.15[v]'
    else:
        args+=['-ss',str(item['start']),'-i',item['source']]
        overlay=label(item['name'],item['title'],item.get('subtitle',''),mode,item.get('body',()))
        args+=['-loop','1','-i',str(ART/'background.png'),'-loop','1','-i',str(overlay)]
        crop=item.get('crop');c=f'crop={crop[2]}:{crop[3]}:{crop[0]}:{crop[1]},' if crop else ''
        if mode=='feature':
            vw=1190;vh=round((crop[3]/crop[2] if crop else 720/1280)*vw/2)*2
            if vh>870:vw=round(vw*870/vh/2)*2;vh=870
            x=640+(1190-vw)//2;y=(H-vh)//2
            chain=f'[0:v]fps=30,{c}scale={vw}:{vh}:flags=lanczos,setsar=1,setpts=PTS-STARTPTS[g];[1:v][g]overlay={x}:{y}:shortest=1[b];'
        elif mode=='cinema':
            vw=1920;vh=round((crop[3]/crop[2] if crop else 720/1280)*vw/2)*2
            if vh>856:vw=round(vw*856/vh/2)*2;vh=856
            chain=f'[0:v]fps=30,{c}scale={vw}:{vh}:flags=lanczos,setsar=1,setpts=PTS-STARTPTS[g];[1:v][g]overlay={(W-vw)//2}:{(H-vh)//2}:shortest=1[b];'
        else:
            chain=f'[0:v]fps=30,{c}scale=1808:1016:flags=lanczos,setsar=1,setpts=PTS-STARTPTS[g];[1:v][g]overlay=56:32:shortest=1[b];'
        chain+=f'[b][2:v]overlay=0:0:shortest=1,fade=t=in:st=0:d=0.12,fade=t=out:st={duration-.12}:d=0.12[v]'
    args+=['-filter_complex',chain,'-map','[v]','-t',str(duration),'-an','-r','30','-c:v','libx264','-preset','fast','-crf','17','-threads','3','-pix_fmt','yuv420p','-video_track_timescale','15360',str(output)]
    result=subprocess.run(args,capture_output=True,text=True)
    if result.returncode:raise RuntimeError(item['name']+': '+result.stderr[-3000:])
    stamp.write_text(signature)
    print('RENDERED',item['name'],duration,flush=True)
    return output


def join(plan,name):
    playlist=CUTS/(name+'.txt')
    playlist.write_text(''.join("file '"+str(CUTS/(item['name']+'.mp4'))+"'\n" for item in plan))
    output=CUTS/(name+'-picture.mp4')
    subprocess.run(['ffmpeg','-nostdin','-hide_banner','-loglevel','error','-y','-f','concat','-safe','0','-i',str(playlist),'-map','0:v','-c','copy',str(output)],check=True)
    return output


def main():
    for p in [ART,CUTS,DELIVERY]:p.mkdir(parents=True,exist_ok=True)
    background();title_card();title_card(True)
    plan=make_plan()
    timeline=0
    for item in plan:item['timeline']=round(timeline,3);timeline+=item['duration']
    (WORK/'edit-decision-list.json').write_text(json.dumps(plan,indent=2)+'\n')
    print('DURATION',timeline,flush=True)
    with ThreadPoolExecutor(max_workers=3) as pool:
        futures={pool.submit(render_segment,item):item['name'] for item in plan}
        for f in as_completed(futures):f.result()
    output=join(plan,'showcase')
    print('PICTURE',output,flush=True)
    teaser_names=['01-cold-open','03-title','04-four-players','05-steel','07-fireflash','09-turns','12-log','13-drag','14-resize','15-alpha','18-inventory','27-tarant','28-qintarra','31-closing-fight','32-end']
    teaser=[next(item for item in plan if item['name']==name) for name in teaser_names]
    (WORK/'teaser-decision-list.json').write_text(json.dumps(teaser,indent=2)+'\n')
    print('TEASER_PICTURE',join(teaser,'teaser'),flush=True)


if __name__=='__main__':main()
