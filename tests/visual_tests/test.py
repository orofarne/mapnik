#!/usr/bin/env python
# -*- coding: utf-8 -*-

import mapnik
import sys
import os.path
from compare import compare, summary

defaults = {
    'sizes': [(500, 100)]
}

sizes_many_in_big_range = [(800, 100), (600, 100), (400, 100),
    (300, 100), (250, 100), (150, 100), (100, 100)]

sizes_few_square = [(800, 800), (600, 600), (400, 400), (200, 200)]
sizes_many_in_small_range = [(490, 100), (495, 100), (497, 100), (498, 100),
    (499, 100), (500, 100), (501, 100), (502, 100), (505, 100), (510, 100)]

default_text_box = mapnik.Box2d(-0.05, -0.01, 0.95, 0.01)

dirname = os.path.dirname(__file__)

files = [
    {'name': "list", 'sizes': sizes_many_in_big_range,'bbox':default_text_box},
    {'name': "simple", 'sizes': sizes_many_in_big_range,'bbox':default_text_box},
    {'name': "lines-1", 'sizes': sizes_few_square,'bbox':default_text_box},
    {'name': "lines-2", 'sizes': sizes_few_square,'bbox':default_text_box},
    {'name': "lines-3", 'sizes': sizes_few_square,'bbox':default_text_box},
    {'name': "lines-4", 'sizes': sizes_few_square},
    {'name': "lines-5", 'sizes': sizes_few_square},
    {'name': "lines-6", 'sizes': sizes_few_square},
    {'name': "lines-shield", 'sizes': sizes_few_square,'bbox':default_text_box},
    {'name': "simple-E", 'bbox':mapnik.Box2d(-0.05, -0.01, 0.95, 0.01)},
    {'name': "simple-NE",'bbox':default_text_box},
    {'name': "simple-NW",'bbox':default_text_box},
    {'name': "simple-N",'bbox':default_text_box},
    {'name': "simple-SE",'bbox':default_text_box},
    {'name': "simple-SW",'bbox':default_text_box},
    {'name': "simple-S",'bbox':default_text_box},
    {'name': "simple-W",'bbox':default_text_box},
    {'name': "formatting-1",'bbox':default_text_box},
    {'name': "formatting-2",'bbox':default_text_box},
    {'name': "formatting-3",'bbox':default_text_box},
    {'name': "formatting-4",'bbox':default_text_box},
    {'name': "formatting", 'bbox':default_text_box},
    {'name': "expressionformat",'bbox':default_text_box},
    {'name': "shieldsymbolizer-1", 'sizes': sizes_many_in_small_range,'bbox':default_text_box},
    {'name': "rtl-point", 'sizes': [(200, 200)],'bbox':default_text_box},
    {'name': "jalign-auto", 'sizes': [(200, 200)],'bbox':default_text_box},
    {'name': "line-offset", 'sizes':[(900, 250)],'bbox': mapnik.Box2d(-5.192, 50.189, -5.174, 50.195)},
    {'name': "tiff-alpha-gdal", 'sizes':[(600,400)]},
    {'name': "tiff-alpha-raster", 'sizes':[(600,400)]},
    {'name': "shieldsymbolizer-2"},
    {'name': "shieldsymbolizer-3"},
    {'name': "shieldsymbolizer-4"},
    {'name': "orientation", 'sizes': [(800, 200)]},
    {'name': "hb-fontsets", 'sizes': [(800, 200)]},
    {'name': "charspacing", 'sizes': [(200, 400)]},
    {'name': "line_break", 'sizes': [(800, 800)]},
    ]

def render(filename, width, height, bbox, quiet=False):
    if not quiet:
        print "Rendering style \"%s\" with size %dx%d ... \x1b[1;32m✓ \x1b[0m" % (filename, width, height)
        print "-"*80
    m = mapnik.Map(width, height)
    mapnik.load_map(m, os.path.join(dirname, "styles", "%s.xml" % filename), False)
    if bbox is not None:
        m.zoom_to_box(bbox)
    else:
        m.zoom_all()
    expected = os.path.join(dirname, "images", '%s-%d-reference.png' % (filename, width))
    if not os.path.exists('/tmp/mapnik-visual-images'):
        os.makedirs('/tmp/mapnik-visual-images')
    actual = os.path.join("/tmp/mapnik-visual-images", '%s-%d-agg.png' % (filename, width))
    mapnik.render_to_file(m, actual)
    diff = compare(actual, expected)
    if diff > 0:
        print "-"*80
        print '\x1b[33mError:\x1b[0m %u different pixels' % diff
        print "-"*80

    return m

if __name__ == "__main__":
    if '-q' in sys.argv:
       quiet = True
       sys.argv.remove('-q')
    else:
       quiet = False

    if len(sys.argv) <= 1:
        active = files
    elif len(sys.argv) == 2:
        active = [{"name": sys.argv[1], "sizes": sizes_few_square}]
    elif len(sys.argv) > 2:
        active = []
        if sys.argv[1] == "-s":
            name = sys.argv[2]
            for f in files:
                if f['name'] == name:
                    active.append(f)
        else:
            for name in sys.argv[1:]:
                active.append({"name": name})

    if 'osm' in mapnik.DatasourceCache.plugin_names():
        for f in active:
            config = dict(defaults)
            config.update(f)
            for size in config['sizes']:
                m = render(config['name'], size[0], size[1], config.get('bbox'), quiet=quiet)
            mapnik.save_map(m, os.path.join(dirname, 'xml_output', "%s-out.xml" % config['name']))

        summary(generate=False)
