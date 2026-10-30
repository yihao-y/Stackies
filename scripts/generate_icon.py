#!/usr/bin/env python3
"""Build a multi-resolution Windows ICO from the Sticky Notes Stack artwork.
Standard-library only; no downloaded assets or external image tools required.
The 16/20/24px frames intentionally omit text lines and paper layers.
"""
import math
import struct
import sys
import zlib
from pathlib import Path

def inside_round(x, y, left, top, width, height, radius):
    if x < left or x > left + width or y < top or y > top + height:
        return False
    cx = max(left + radius, min(x, left + width - radius))
    cy = max(top + radius, min(y, top + height - radius))
    return (x - cx) ** 2 + (y - cy) ** 2 <= radius ** 2

def inside_line(x, y, x1, y1, x2, y2, width):
    dx, dy = x2 - x1, y2 - y1
    t = max(0.0, min(1.0, ((x-x1)*dx+(y-y1)*dy)/(dx*dx+dy*dy)))
    return (x-x1-dx*t)**2+(y-y1-dy*t)**2 <= (width/2)**2

def rgb(code):
    return tuple(bytes.fromhex(code.lstrip("#")))

def artwork(small):
    shapes = []
    def box(x,y,w,h,r,fill,stroke=None,border=1):
        if stroke:
            shapes.append(("rect",(x,y,w,h,r),rgb(stroke),1.0))
            shapes.append(("rect",(x+border,y+border,w-2*border,h-2*border,max(0,r-border)),
                           rgb(fill),1.0))
        else:
            shapes.append(("rect",(x,y,w,h,r),rgb(fill),1.0))
    if not small:
        box(12,13,56,60,7,"#DECEAB","#A69879",1.4)
        box(16,9,49,62,6,"#E7DCC2","#AA9C7C",1.2)
        for y,color in [(17,"#88BADA"),(35,"#8CC9A7"),(53,"#E6A2B1")]:
            box(60,y,14,12,3,color)
        box(9,7,53,64,7,"#F8E5A0","#AB9868",1.5)
        for y,length in [(29,29),(37,26),(45,20)]:
            shapes.append(("line",(20,y,20+length,y,2.4),rgb("#B9A56D"),0.65))
    else:
        for y,color in [(13,"#88BADA"),(33,"#8CC9A7"),(53,"#E6A2B1")]:
            box(58,y,16,15,3,color)
        box(8,8,53,63,7,"#F8E5A0","#A18D61",2)
    return shapes

def png_chunk(tag, value):
    return struct.pack(">I",len(value))+tag+value+struct.pack(">I",zlib.crc32(tag+value)&0xffffffff)

def png_frame(size):
    shapes=artwork(size < 32)
    raw=bytearray()
    # Four subpixel samples avoid aliasing on the rounded outer silhouette.
    offsets=(0.25,0.75)
    for py in range(size):
        raw.append(0)
        for px in range(size):
            totals=[0.0,0.0,0.0,0.0]
            for oy in offsets:
                for ox in offsets:
                    x=(px+ox)*80.0/size
                    y=(py+oy)*80.0/size
                    c=[0.0,0.0,0.0]
                    a=0.0
                    for kind,shape,color,opacity in shapes:
                        if kind=="rect":
                            covers=inside_round(x,y,*shape)
                        else:
                            covers=inside_line(x,y,*shape)
                        if covers:
                            a=opacity+(1-opacity)*a
                            c=[color[k]*opacity+c[k]*(1-opacity) for k in range(3)]
                    for k in range(3):
                        totals[k]+=c[k]
                    totals[3]+=a*255
            # PNG stores unpremultiplied RGB, so remove the edge alpha
            # weighting before encoding to avoid dark fringes on rounded tabs.
            if totals[3]:
                raw.extend(max(0,min(255,round(totals[k]*255/totals[3])))
                           for k in range(3))
            else:
                raw.extend((0,0,0))
            raw.append(max(0,min(255,round(totals[3]/4))))
    signature=b"\x89PNG\r\n\x1a\n"
    ihdr=struct.pack(">IIBBBBB",size,size,8,6,0,0,0)
    return (signature+png_chunk(b"IHDR",ihdr)+
            png_chunk(b"IDAT",zlib.compress(bytes(raw),9))+
            png_chunk(b"IEND",b""))

def make_ico(output):
    sizes=(16,20,24,32,48,64,128,256)
    frames=[png_frame(size) for size in sizes]
    header=struct.pack("<HHH",0,1,len(frames))
    pos=6+16*len(frames)
    directory=bytearray()
    for size,frame in zip(sizes,frames):
        directory+=struct.pack("<BBBBHHII",size if size<256 else 0,
                               size if size<256 else 0,0,0,1,32,len(frame),pos)
        pos+=len(frame)
    Path(output).parent.mkdir(parents=True,exist_ok=True)
    Path(output).write_bytes(header+directory+b"".join(frames))

if __name__=="__main__":
    if len(sys.argv)!=2:
        raise SystemExit("Usage: generate_icon.py OUTPUT.ico")
    make_ico(sys.argv[1])
