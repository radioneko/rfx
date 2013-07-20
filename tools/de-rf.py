#! /usr/bin/env python2.7
import Image, ImageDraw
from StringIO import StringIO
from urllib import urlopen
from urlparse import urlparse
import os
import sys
from lxml import etree


def fetch(src, outdir):
    outfn = outdir + '/' + os.path.basename(urlparse(src).path)
    if os.path.exists(outfn):
        return
    print('Fetching ' + src + ' => ' + outfn)
    h = urlopen(src)
    try:
        data = h.read()
        dat = StringIO(data)
        im = Image.open(dat)
        im = im.crop((4, 4, 68, 68))
        draw = ImageDraw.Draw(im)
        w = [25, 26, 27, 29, 30, 32]
        y = 63
        for x in w:
            draw.line((x, y, 63, y), fill=0)
            y -= 1
        im.save(outfn)
    finally:
        h.close()


for src in sys.argv[1:]:
    doc = etree.parse(src)
    root = doc.getroot()
    for i in root:
        if i.tag == 'item' and i.get('img'):
            try:
                fetch(i.get('img'), '/tmp/pic')
            except:
                print('        FAILED')
