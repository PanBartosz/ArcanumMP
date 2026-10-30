#!/usr/bin/env python3
"""Mix the locally installed original score/SFX with restrained transition Foley.

No desktop audio, microphone, borrowed trailer soundtrack or synthesized
gameplay sound is recorded. Generated whooshes/impacts are editorial transitions.
"""
import json
from pathlib import Path
import subprocess
import numpy as np
from scipy import signal
from scipy.io import wavfile
from render import DELIVERY, WORK, CUTS

ROOT=Path(__file__).resolve().parents[2]
AUDIO=ROOT/'evidence/showcase/audio';DEST=DELIVERY;SR=48000


def decode(name):
    raw=subprocess.check_output(['ffmpeg','-nostdin','-hide_banner','-loglevel','error','-i',str(AUDIO/name),'-f','f32le','-ac','2','-ar',str(SR),'-'])
    return np.frombuffer(raw,dtype='<f4').reshape(-1,2).copy()


def fade(data,fade_in=.2,fade_out=.5):
    data=data.copy();a=min(len(data),round(fade_in*SR));b=min(len(data),round(fade_out*SR))
    if a:data[:a]*=np.sin(np.linspace(0,np.pi/2,a))[:,None]**2
    if b:data[-b:]*=np.cos(np.linspace(0,np.pi/2,b))[:,None]**2
    return data


def music_segment(name,start,duration):
    data=decode(name);a=round(start*SR);b=a+round(duration*SR);data=data[a:b]
    rms=np.sqrt(np.mean(data**2));data*=.105/max(.01,rms)
    return fade(data,.7,1.0)


def impact():
    rng=np.random.default_rng(19);t=np.arange(round(SR*1.6))/SR
    bass=np.sin(2*np.pi*(63*t-10*t*t))*np.exp(-6*t)
    metal=signal.sosfilt(signal.butter(3,[500,3000],btype='bandpass',fs=SR,output='sos'),rng.normal(0,1,len(t)))*np.exp(-22*t)
    return np.stack([bass*.15+metal*.08,bass*.15+metal*.06],axis=1).astype('f4')


def whoosh():
    rng=np.random.default_rng(71);t=np.arange(round(SR*.8))/SR
    noise=signal.sosfilt(signal.butter(3,[170,2600],btype='bandpass',fs=SR,output='sos'),rng.normal(0,1,len(t)))
    env=np.sin(np.linspace(0,np.pi,len(t)))**3
    data=noise*env*.055
    return np.stack([data,np.roll(data,170)],axis=1).astype('f4')


def mix(plan,name):
    duration=sum(x['duration'] for x in plan);result=np.zeros((round(duration*SR),2),dtype='f4');cues=[]
    def put(data,at,gain=1):
        offset=round(at*SR);lo=max(0,-offset);hi=min(len(data),len(result)-offset)
        if hi>lo:result[max(offset,0):offset+hi]+=data[lo:hi]*gain
    if name=='showcase':
        music=[(str(WORK/'Five-Armies.mp3'),0,0,18.5),(str(WORK/'Arcadia.mp3'),0,17,88),(str(WORK/'Five-Armies.mp3'),156-(duration-103),103,duration-103)]
    else:
        music=[(str(WORK/'Five-Armies.mp3'),12,0,duration)]
    for asset,source,at,length in music:
        put(music_segment(asset,source,length),at);cues.append(dict(kind='score',asset=asset,source=source,at=at,duration=length))
    clock=0;native_cache={}
    for item in plan:
        kind=item['name']
        if kind in ['03-title','04-four-players','32-end']:
            put(impact(),clock,.60);put(whoosh(),max(0,clock-.3),.8)
        if kind=='07-fireflash':
            for asset,delta,gain in [('Spell_Elemental_Fire_Fireflash.wav',.12,.32),('Spell_Elemental_Fire_Fireflash_Impact.wav',.85,.32)]:
                native_cache.setdefault(asset,decode(asset));data=native_cache[asset]
                peak=np.max(np.abs(data));put(data/(peak or 1)*.20,clock+delta,gain)
                cues.append(dict(kind='native-sfx',asset=asset,at=clock+delta))
        if kind in ['01-cold-open','31-closing-fight']:
            asset='Spell_Elemental_Fire_Fireflash_Impact.wav';native_cache.setdefault(asset,decode(asset))
            data=native_cache[asset];put(data/max(.01,np.max(np.abs(data)))*.09,clock+.65)
            cues.append(dict(kind='native-sfx',asset=asset,at=clock+.65))
        if kind.startswith(('12-','13-','14-','15-','16-','17-','18-','19-','20-','21-','22-','23-','25-')):
            asset='book_open.wav' if kind.startswith(('12-','16-','20-')) else 'Button_Metal.wav'
            native_cache.setdefault(asset,decode(asset));data=native_cache[asset];peak=np.max(np.abs(data));put(data/(peak or 1)*.055,clock+.35)
            cues.append(dict(kind='native-sfx',asset=asset,at=clock+.35))
        if kind.startswith(('27-','28-','29-','30-')):put(whoosh(),clock,.35)
        clock+=item['duration']
    result=fade(result,.15,1.9)
    # Headroom before the final measured loudness/true-peak pass.
    peak=np.max(np.abs(result));result*=min(1,.91/max(.01,peak))
    wav=CUTS/(name+'-mix.wav');wavfile.write(wav,SR,result)
    (WORK/(name+'-audio-cues.json')).write_text(json.dumps(cues,indent=2)+'\n')
    picture=CUTS/(name+'-picture.mp4')
    dest=DEST/('ArcanumMP-showcase-1080p.mp4' if name=='showcase' else 'ArcanumMP-trailer-1080p.mp4')
    cmd=['ffmpeg','-nostdin','-hide_banner','-loglevel','info','-y','-i',str(picture),'-i',str(wav),'-map','0:v:0','-map','1:a:0','-c:v','copy','-af','loudnorm=I=-16:TP=-1.5:LRA=10:print_format=json','-ar','48000','-c:a','aac','-b:a','192k','-t',str(duration),'-movflags','+faststart','-metadata','title=ArcanumMP — Campaign co-op','-metadata','comment=Native gameplay; staged encounters; four independent Linux engines over TCP. Music: Arcadia and Five Armies by Kevin MacLeod, incompetech.com, CC BY 4.0. Original game: Troika Games / Sierra.',str(dest)]
    with (WORK/(name+'-mix.log')).open('w') as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
    print('FINISHED',dest,duration,dest.stat().st_size,flush=True)


if __name__=='__main__':
    for filename,name in [('edit-decision-list.json','showcase'),('teaser-decision-list.json','teaser')]:mix(json.loads((WORK/filename).read_text()),name)
