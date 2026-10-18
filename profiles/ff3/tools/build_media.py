#!/usr/bin/env python3
"""Build the FF3 host's minimal LGPL FFmpeg dependency in a local output folder."""
import argparse
import hashlib
import platform
import shutil
import subprocess
import tarfile
import urllib.request
from pathlib import Path
VERSION='7.1.1'
SHA256='733984395e0dbbe5c046abda2dc49a5544e7e0e1e2366bba849222ae9e3a03b1'
def build(root):
    root=root.resolve();root.mkdir(parents=True,exist_ok=True)
    archive=root/('ffmpeg-'+VERSION+'.tar.xz')
    if not archive.exists():urllib.request.urlretrieve('https://ffmpeg.org/releases/'+archive.name,archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest()!=SHA256:raise ValueError('FFmpeg archive hash mismatch')
    source=root/('ffmpeg-'+VERSION)
    if not source.exists():
        with tarfile.open(archive) as tar:
            for member in tar.getmembers():
                target=(root/member.name).resolve()
                if root not in target.parents or member.issym() or member.islnk():raise ValueError('Unsafe archive member')
            tar.extractall(root)
    # H264_SEI references the common film-grain destructor even in this minimal
    # configuration. FFmpeg 7.1.1 does not pull in its object automatically.
    makefile=source/'libavcodec/Makefile';text=makefile.read_text()
    text=text.replace('+= h264_sei.o h2645_sei.o\n','+= h264_sei.o h2645_sei.o aom_film_grain.o\n');makefile.write_text(text)
    # Apple Silicon uses the compiler's assembler. Intel hosts without NASM
    # can still build the portable C implementation.
    assembler=[] if platform.machine().lower() not in ('x86_64','amd64') or shutil.which('nasm') else ['--disable-x86asm']
    subprocess.run(['./configure','--prefix='+str(root/'install'),*assembler,
        '--disable-everything','--disable-programs','--disable-doc','--disable-debug',
        '--disable-autodetect','--disable-avdevice','--disable-avfilter',
        '--disable-static','--enable-shared','--enable-small',
        '--enable-decoder=atrac3,atrac3p,h264,aac,pcm_s16le',
        '--enable-demuxer=wav,mpegps,h264','--enable-parser=h264,aac',
        '--enable-protocol=file','--enable-swresample','--enable-swscale'],cwd=source,check=True)
    subprocess.run(['make','-j1'],cwd=source,check=True)
    subprocess.run(['make','install'],cwd=source,check=True)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=Path);build(p.parse_args().output)
