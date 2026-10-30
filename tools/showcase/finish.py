#!/usr/bin/env python3
"""Make the small sharing copy, poster, captions, credits and final media audit."""
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import re
from pathlib import Path
import struct
import subprocess

from PIL import Image,ImageDraw
from render import ROOT,WORK,ART,DELIVERY as DEST,font,IVORY,GOLD


def run(args,log):
    with log.open('w') as output:subprocess.run(args,stdout=output,stderr=subprocess.STDOUT,check=True)


def compact():
    source=DEST/'ArcanumMP-trailer-1080p.mp4';output=DEST/'ArcanumMP-trailer-720p-small.mp4'
    if output.exists() and output.stat().st_mtime>=source.stat().st_mtime and 0<output.stat().st_size<9_500_000:
        return
    base=['ffmpeg','-nostdin','-hide_banner','-loglevel','warning','-y','-i',str(source),'-map','0:v:0','-vf','scale=1280:720:flags=lanczos','-c:v','libx264','-b:v','1080k','-preset','slow','-threads','5','-pix_fmt','yuv420p','-passlogfile',str(WORK/'edit/compact-pass')]
    run(base+['-pass','1','-an','-f','null','/dev/null'],WORK/'compact-pass-1.log')
    run(base+['-pass','2','-map','0:a:0','-c:a','aac','-b:a','96k','-ar','48000','-movflags','+faststart',str(output)],WORK/'compact-pass-2.log')
    assert output.stat().st_size<9_500_000,output.stat().st_size
    print('COMPACT',output,output.stat().st_size,flush=True)


def poster():
    im=Image.open(ART/'background.png').convert('RGBA')
    take=ROOT/'evidence/showcase/takes/launch-v3';meta=json.loads((take/'capture.json').read_text())['peers']
    for i,(x,y) in enumerate([(780,238),(1325,238),(780,560),(1325,560)]):
        frame=WORK/f'poster-peer-{i}.png';at=next(e['frame']/30+2.0 for e in json.loads((take/f'actions-{i}.json').read_text()) if e['name']=='independent-paths')
        subprocess.run(['ffmpeg','-nostdin','-hide_banner','-loglevel','error','-y','-ss',str(at),'-i',str(take/f'peer-{i}.mp4'),'-frames:v','1',str(frame)],check=True)
        gameplay=Image.open(frame).resize((520,292),Image.Resampling.LANCZOS).convert('RGBA')
        im.alpha_composite(gameplay,(x,y));d=ImageDraw.Draw(im)
        d.rectangle((x-2,y-2,x+522,y+294),outline=GOLD,width=1)
        d.text((x,y-25),['HOST','PLAYER 2','PLAYER 3','PLAYER 4'][i],font=font(17,sans=True),fill=GOLD)
    d=ImageDraw.Draw(im)
    d.text((88,236),'ARCANUM',font=font(112,True),fill=GOLD,stroke_width=1)
    d.text((93,375),'WITH FRIENDS.',font=font(52,True),fill=IVORY)
    d.line((95,464,455,464),fill=GOLD,width=2)
    d.text((95,510),'4-PLAYER',font=font(36,bold=True,sans=True),fill=IVORY)
    d.text((95,566),'CAMPAIGN CO-OP',font=font(36,bold=True,sans=True),fill=IVORY)
    d.text((95,683),'Windows + Linux',font=font(26,sans=True),fill=(170,166,151,255))
    d.text((95,872),'ArcanumMP',font=font(27,sans=True),fill=GOLD)
    d.text((95,924),'An old world. New company.',font=font(24),fill=IVORY)
    im.convert('RGB').save(DEST/'ArcanumMP-thumbnail.jpg',quality=95,subsampling=0)
    Image.open(ART/'title.png').save(DEST/'ArcanumMP-title.png')


def timestamp(seconds):
    ms=round(seconds*1000);hours,ms=divmod(ms,3600000);minutes,ms=divmod(ms,60000);secs,ms=divmod(ms,1000)
    return f'{hours:02}:{minutes:02}:{secs:02},{ms:03}'


def captions():
    for source,name in [('edit-decision-list.json','showcase'),('teaser-decision-list.json','trailer')]:
        plan=json.loads((WORK/source).read_text());clock=0;lines=[]
        for i,item in enumerate(plan,1):
            title=item.get('title','ArcanumMP — Original campaign, together.');sub=item.get('subtitle','')
            if item.get('body'):sub=' '.join(item['body'])
            lines.append(f'{i}\n{timestamp(clock)} --> {timestamp(clock+item["duration"]-.06)}\n{title}'+ ('\n'+sub if sub else '')+'\n')
            clock+=item['duration']
        (DEST/f'ArcanumMP-{name}.srt').write_text('\n'.join(lines)+'\n')


def audit(path):
    info=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(path)],text=True))
    log=WORK/(path.stem+'-decode.log')
    run(['ffmpeg','-nostdin','-hide_banner','-loglevel','info','-i',str(path),'-vf','blackdetect=d=0.35:pix_th=0.01:pic_th=0.995','-af','ebur128=peak=true','-f','null','-'],log)
    contents=log.read_text(errors='replace')
    streams=info['streams'];video=next(s for s in streams if s['codec_type']=='video');audio=next(s for s in streams if s['codec_type']=='audio')
    atoms=[]
    with path.open('rb') as stream:
        offset=0
        while offset<path.stat().st_size:
            stream.seek(offset);header=stream.read(8)
            if len(header)<8:break
            size,kind=struct.unpack('>I4s',header)
            if size==1:size=struct.unpack('>Q',stream.read(8))[0]
            atoms.append(kind.decode('ascii',errors='replace'))
            if size<8:break
            offset+=size
    assert atoms.index('moov')<atoms.index('mdat'),atoms
    assert video['codec_name']=='h264' and video['pix_fmt']=='yuv420p' and video['r_frame_rate']=='30/1'
    assert audio['codec_name']=='aac' and audio['channels']==2
    assert 'Error while decoding' not in contents and 'Invalid data found' not in contents
    last=contents.rsplit('Summary:',1)[-1].strip()
    levels={key:float(re.search(pattern,last)[1]) for key,pattern in [('integrated_lufs',r'I:\s+(-?[0-9.]+) LUFS'),('loudness_range_lu',r'LRA:\s+([0-9.]+) LU'),('true_peak_dbfs',r'Peak:\s+(-?[0-9.]+) dBFS')]}
    result=dict(file=path.name,bytes=path.stat().st_size,sha256=hashlib.file_digest(path.open('rb'),'sha256').hexdigest(),duration=float(info['format']['duration']),width=video['width'],height=video['height'],frames=int(video['nb_frames']),fps=video['r_frame_rate'],video=video['codec_name'],pixel_format=video['pix_fmt'],audio=audio['codec_name'],audio_channels=audio['channels'],audio_sample_rate=audio['sample_rate'],fast_start=True,full_decode=True,audio_measurements=levels,unexpected_black_spans=[line.strip() for line in contents.splitlines() if 'black_start:' in line])
    print('VERIFIED',path.name,result['duration'],result['bytes'],flush=True)
    return result


def readme(reports):
    text="""# ArcanumMP — gameplay films

First public release, filmed with the current polished interface.

- `ArcanumMP-trailer-1080p.mp4`: the short trailer.
- `ArcanumMP-showcase-1080p.mp4`: the full feature showcase.
- `ArcanumMP-trailer-720p-small.mp4`: attachment-sized copy.
- Matching cover, captions and the file/codec/audio manifest.

## Footage

Four real native Linux engines connected over TCP. Each player follows separate
routes in Tarant; the party then fights native orcs and an ore golem outside
Tulla. Actors, gear, statistics, encounters and travel were prepared for filming.
These are staged encounters, not a normal-party balance run. The software is
unmodified: the filming helper is loaded only into disposable media runtimes.

The additional feature scenes use the current interface in Tarant. The movement
and resize segments record the actual continuously held mouse gesture, frame by
frame. The camera pans use native pixel scrolling. All gameplay footage is
captured from completed native frames at 30 fps; no interpolated gameplay frames
or simulated multiplayer panels are used. Split views, crops and titles are edits.

## Credits

Original game, artwork and native effects: Arcanum: Of Steamworks & Magick
Obscura, Troika Games / Sierra Entertainment. Original game data is required
separately; no loose game music or effects are included in this delivery.
Engine: Arcanum Community Edition by alexbatalov and contributors, modified as
ArcanumMP; Sustainable Use License 1.0. Project artwork: assets/ui/README.md.

"Arcadia" and "Five Armies" — Kevin MacLeod (incompetech.com).
Licensed under Creative Commons: By Attribution 4.0.
https://creativecommons.org/licenses/by/4.0/
Sources: https://incompetech.com/music/royalty-free/index.html?isrc=USUAN1100326
and https://incompetech.com/music/royalty-free/index.html?Search=Search&isrc=USUAN1100875
The recordings were excerpted, mixed, faded and normalized for these edits.
Additional transition Foley was generated for the edit.

## Share

Project: https://github.com/PanBartosz/ArcanumMP
Website: https://panbartosz.github.io/ArcanumMP/

Suggested title: Arcanum's original campaign in four-player co-op — Windows and Linux

ArcanumMP adds four-player campaign co-op, with your own characters and shared
travel, dialogue and saves. It also adds a customizable interface, combat log,
portrait targeting, mouse-wheel zoom, searchable inventory/actions and quest
notes. Many of the interface improvements also work in single-player.

This is staged gameplay from actual connected engines. The mod is free, and
each player needs the original game. Feedback and bug reports are welcome.
"""
    (DEST/'README.md').write_text(text)


def main():
    compact();poster();captions()
    files=sorted(DEST.glob('*.mp4'))
    with ThreadPoolExecutor(max_workers=3) as pool:reports=list(pool.map(audit,files))
    metadata={'created':'2026-10-30','edit_revision':3,'game_source':'fc6035e097246b4e952bef2c232db4bdb0667b82','protocol':59,'native_gameplay':True,'staged_development_encounters':True,'multiplayer_capture':'four independent native Linux engines, TCP loopback','files':reports}
    cadence=WORK/'cadence-v3.json'
    if cadence.exists():
        (DEST/cadence.name).write_text(cadence.read_text())
        metadata['motion_cadence_report']=cadence.name
    (DEST/'manifest.json').write_text(json.dumps(metadata,indent=2)+'\n')
    (WORK/'delivery.json').write_text(json.dumps(metadata,indent=2)+'\n')
    readme(reports)


if __name__=='__main__':main()
