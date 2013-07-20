#! /usr/bin/env python2.7
# -*- coding: utf-8 -*
from bs4 import BeautifulSoup
from urllib import urlopen
from lxml import etree
import sys
import os
import pprint

RACE_ACC = 0
RACE_BCC = 1
RACE_CCC = 2

CLASS_MEELE = 1
CLASS_RANGE = 2
CLASS_MAGE = 3
CLASS_LAUCH = 4

IDX_CODE = 0
IDX_RACE = 1
IDX_LEVEL = 2
IDX_ATK = 3
IDX_F_ATK = 4
IDX_DEF = 5
IDX_PT = 6
IDX_PROP1 = 7
IDX_PROP2 = 8
IDX_PROP3 = 9
IDX_PROP4 = 10

def parse_percent(s):
    p = s.find('%') - 1
    r = ''
    while p >= 0 and s[p].isdigit():
        r = s[p] + r
        p -= 1
    return r

def parse_plus(s):
    p = s.find('+') + 1
    r = ''
    if p > 0:
        l = len(s)
        while p < l and s[p].isdigit():
            r = r + s[p]
            p += 1
    return r

class Item:
    def __init__(self, kv):
        self.kv = kv

    def copy_int(self, idx, name, prop):
        try:
            v = int(self.kv[idx])
            if v > 0:
                prop[name] = str(v)
        except:
            pass

    def copy_prop(self, idx, name, prop, tag):
        try:
            p = self.kv[idx]
            if p == u'Бег +1.0':
                tag.add('wind')
            elif p.find(u'Вампиризм:') != -1:
                tag.add('vamp')
                tag.add('va' + parse_percent(p))
            elif p.find(u'Урон +') != -1:
                tag.add('atk')
                tag.add('at' + parse_percent(p))
            elif p.find(u'Уворот +') != -1:
                tag.add('dodge')
                tag.add('do' + parse_plus(p))
            elif p.find(u'Защита +') != -1:
                tag.add('def')
                tag.add('de' + parse_percent(p))
            elif p.find(u'Точность +') != -1:
                tag.add('accuracy')
                tag.add('ac' + parse_plus(p))
            elif p.find(u'Радиус +') != -1:
                tag.add('radius')
                tag.add('ra' + parse_plus(p))
            prop[name] = p
        except:
            pass

    def set_tags(self, prop, tag):
        try:
            prop['code'] = self.kv[IDX_CODE]
        except:
            pass
        # set race tags
        try:
            r = self.kv[IDX_RACE]
            if r == u'Все':
                tag.add('r:a')
                tag.add('r:b')
                tag.add('r:c')
            elif r == u'Акретия':
                tag.add('r:a')
            elif r == u'Беллато':
                tag.add('r:b')
            elif r == u'Кора':
                tag.add('r:c')
        except:
            pass
        # set class tags
        try:
            pt = self.kv[IDX_PT]
            if pt.find(u' ББ') != -1:
                tag.add('meele')
            elif pt.find(u' ДД') != -1:
                tag.add('ranger')
            elif pt.find(u' МАГ') != -1:
                tag.add('mage')
            elif pt.find(u' ПУ') != -1:
                tag.add('launcher')
        except:
            pass
        # set level property
        self.copy_int(IDX_LEVEL, 'level', prop)
        # set attack properties
        self.copy_int(IDX_ATK, 'atk', prop)
        self.copy_int(IDX_F_ATK, 'fatk', prop)
        # set def property
        self.copy_int(IDX_DEF, 'def', prop)
        # set prop1
        self.copy_prop(IDX_PROP1, 'p1', prop, tag)
        self.copy_prop(IDX_PROP2, 'p2', prop, tag)
        self.copy_prop(IDX_PROP3, 'p3', prop, tag)
        self.copy_prop(IDX_PROP4, 'p4', prop, tag)



def parse_item(url):
    if url[:7] != 'http://':
        handle = open(url)
    else:
        handle = urlopen(url)
    doc = BeautifulSoup(handle.read())
    handle.close()
    for data in doc.find_all('table'):
        tid= data.get('id')
        if tid == 'main2' or tid == 'amulet' or tid == 'ring':
            break

    # locate columns
    cl = [
        [u'Код', None, IDX_CODE],
        [u'Раса', None, IDX_RACE],
        [u'Уровень', None, IDX_LEVEL],
        [u'Мили Аттака', None, IDX_ATK],
        [u'Маг Аттака', None, IDX_F_ATK],
        [u'Защита', None, IDX_DEF],
        [u'ПТ', None, IDX_PT],
        [u'1й бонус', None, IDX_PROP1],
        [u'2й бонус', None, IDX_PROP2],
        [u'3й бонус', None, IDX_PROP3],
        [u'4й бонус', None, IDX_PROP4]]
    idx = 0
    for th in data.thead.find_all('th'):
        txt = th.text.strip()
        for c in cl:
            if txt == c[0]:
                c[1] = idx
                break
        sp = th.get('colspan')
        if sp:
            idx += int(sp)
        else:
            idx += 1

    idx = 0
    td = data.find_all('tr', limit=2)[1].find_all('td')
    prop = dict()
    for c in cl:
        if c[1] is not None:
            dat = td[c[1]].text.strip()
            if len(dat) != 0 and dat != '-':
                prop[c[2]] = dat

    return Item(prop)

def get_tags(node):
    tags = set()
    for t in node:
        if t.tag == 'tag':
            tags.add(t.text)
    return tags

def apply_tags(node, tags):
    for t in node:
        if t.tag == 'tag':
            node.remove(t)
    for t in tags:
        e = etree.Element('tag')
        e.text = t
        node.append(e)
        #etree.SubElement(node, 'tag').text = t

def apply_props(node, props):
    node.attrib.clear()
    for k, v in props.items():
        node.attrib[k] = v

def save(src, doc):
    dst = "zz/" + os.path.basename(src)
    out = open(dst, 'w')
    print (src + ' => ' + dst)
    out.write(etree.tostring(doc, pretty_print=True, encoding='koi8-r'))
    out.close()

for src in sys.argv[1:]:
    doc = etree.parse(src)
    root = doc.getroot()
    total = len(root)
    pos = 1
    for i in root:
        if i.tag == 'item' and i.get('url'):
            props = dict(i.attrib)
            tags = get_tags(i)
            try:
                sys.stdout.write('  * Parsing item %d of %d: "%s" from %s' % (pos, total, i.get('desc'), i.get('url')))
                p = parse_item(i.get('url'))
                p.set_tags(props, tags)
                apply_props(i, props)
                apply_tags(i, tags)
                print(' [ OK ]')
            except:
                print(' [ FAILED ]')
            pos += 1
    save(src, doc)
