#! /usr/bin/env python2.7
# -*- coding: utf-8 -*
from bs4 import BeautifulSoup
import re
from urllib import urlopen
from urlparse import urljoin
from lxml import etree
import sys
import os
import traceback
import csv

def type_from_string(s):
    if s == u'b' or s == u'B' or s == u'В':
        return u'B' # latin B
    if s == u'c' or s == u'C' or s == u'с' or s == u'С':
        return u'C' # latin C
    if s == u'a' or s == u'A' or s == u'а' or s == u'А':
        return u'A' # latin A
    return s

def prop_from_string(s):
    return s

hex_re = re.compile('<br><br>([0-9a-z]+) =>[^=]*=> ([0-9a-f]+) ')

def hex_id(code):
    d = urlopen('')
    return hex_id

lvl_re = re.compile(u'\[(\d+)\]')
type_re = re.compile(u'\[Тип\s+(.)(-[^\]]*)?\]')

def parse(src, tags, gm2hex):
    if src[:7] != 'http://':
        handle = open(src)
    else:
        handle = urlopen(src)
    doc = BeautifulSoup(handle.read())
    handle.close()
    data = doc.table

    i_tag = list(tags)
    # locate columns with specific names
    desc_idx = 0
    lvl_idx = -1
    code_idx = -1
    idx = 0
    for th in data.thead.find_all('th'):
        txt = th.text.strip()
        if txt == u'Имя':
            desc_idx = idx
        if txt == u'Код':
            code_idx = idx
        if txt == u'Левел':
            lvl_idx = idx
        idx += 1
    
    if code_idx == -1:
        raise RuntimeError('No code columnt found')

    out = etree.Element('root')
    out.set('src', src)

    for row in data.find_all('tr')[1:]:
        td = row.find_all('td')
        img = td[desc_idx].img.get('src')
        url = td[desc_idx].a.get('href')
        desc = td[desc_idx].a.text
        t = type_re.search(desc)
        l = lvl_re.search(desc)
        item = desc.strip()
        code = td[code_idx].text
        e = etree.Element('item')
        if code and code.strip() in gm2hex:
            e.set('hex', gm2hex[code.strip()])
        e.set('desc', item)
        e.set('code', code)
        e.set('img', urljoin(src, img))
        e.set('url', urljoin(src, url))
        tags = list(i_tag)
        if lvl_idx != -1:
            l = td[lvl_idx].text.strip()
            e.set('level', l)
            tags.append('l:' + l)
        elif l:
            e.set('level', l.group(1))
            tags.append('l:' + l.group(1))
        if t:
            tt = type_from_string(t.group(1))
            e.set('type', tt)
            tt == 'C' and tags.append('T:C')
            if tt == 'B':
                tags += ['T:B', 'int']
            tt == 'A' and tags.append('T:A')
            if t.group(2):
                e.set('property', prop_from_string(t.group(2)[1:]))
        for t in set(tags):
            if t:
                tt = etree.Element('tag')
                tt.text = t
                e.append(tt)
        out.append(e)
    return out

fn_re = re.compile('/([^/\.]+)\.[^/]*$')
fn_re2 = re.compile('^([^/\.]+)\.[^/]*$')

def dst_name(src):
    m = fn_re.search(src) or fn_re2.search(src)
    return 'out/' + m.group(1) + '.xml'

def save(src, doc):
    dst = dst_name(src)
    out = open(dst, 'w')
    print (' => ' + dst)
    out.write(etree.tostring(doc, pretty_print=True, encoding='koi8-r'))
    out.close()

def identify(src):
    m = fn_re.search(src) or fn_re2.search(src)
    tag = []
    n = m.group(1)
    t = {
        'Acc': 'r:a',
        'Bcc': 'r:b',
        'Ccc': 'r:c',
        'All': ['r:a', 'r:b', 'r:c'],
        'Heads': ['armor', 'head'],
        'Body' : ['armor', 'body', 'telo'],
        'Throusers': ['armor', 'throusers', 'trusy'],
        'Hands': ['armor', 'hands', 'ruki'],
        'Boots': ['armor', 'boots'],
        'Amulet': ['accessory', 'amulet'],
        'Ring': ['acessory', 'ring'],
        'WeaponAll_0_': ['weapon', 'dagger',   'r:a', 'r:b', 'r:c'], # одноручные мечи/кинжалы
        'WeaponAll_1_': ['weapon', 'sword',    'r:a', 'r:b', 'r:c'], # двуручные мечи
        'WeaponAll_2_': ['weapon', 'axe',      'r:a', 'r:b', 'r:c'], # топоры
        'WeaponAll_3_': ['weapon', 'mace',     'r:a', 'r:b', 'r:c'], # булавы
        'WeaponAll_4_': ['weapon', 'spear',    'r:a', 'r:b', 'r:c'], # копья
        'WeaponAll_5_': ['weapon', 'bow',      'r:a', 'r:b', 'r:c'], # луки
        'WeaponAll_6_': ['weapon', 'firearm',  'r:a', 'r:b', 'r:c'], # огнестрел
        'WeaponAll_7_': ['weapon', 'launcher', 'r:a'], # ПУ
        'WeaponAll_9_': ['weapon', 'staff',    'r:b', 'r:c'], # посохи
    }
    for (k, v) in t.items():
        if n.find(k) != -1:
            if type(v) is list:
                tag += v
            elif type(v) is str:
                tag.append(v)
    return tag

gm2hex = dict()
with open('items.csv', 'rb') as csvfile:
    for line in csvfile:
        row = line.split(';')
        gm2hex[row[1]] = row[0]

for src in sys.argv[1:]:
    sys.stdout.write('Processing ' + src)
    try:
        if os.path.exists(dst_name(src)):
            print ' --> ' + dst_name(src)
        else:
            doc = parse(src, identify(src), gm2hex)
            save(src, doc)
    except:
        print ('  FAILED')
        exc_type, exc_value, exc_traceback = sys.exc_info()
        traceback.print_exception(exc_type, exc_value, exc_traceback)
