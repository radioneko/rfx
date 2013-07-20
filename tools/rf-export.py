#! /usr/bin/env python
from lxml import etree
import sys
import os

with open('/tmp/out.csv', 'wb') as out:
    for src in sys.argv[1:]:
        doc = etree.parse(src)
        root = doc.getroot()
        for i in root:
            if i.tag == 'item':
                p = []
                p.append(i.get('code'))
                p.append(i.get('desc'))
                p.append(i.get('level') or '')
                p.append(i.get('atk') or i.get('def') or '')
                p.append(i.get('fatk') or '')
                p.append(i.get('p1') or '')
                p.append(i.get('p2') or '')
                p.append(i.get('p3') or '')
                p.append(i.get('p4') or '')
                p.append(i.get('img'))
                out.write((';'.join(p) + '\n').encode('utf-8'))
