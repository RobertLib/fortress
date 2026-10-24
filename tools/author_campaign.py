#!/usr/bin/env python3
"""Export explicitly authored rooms and routes; there is no random generation.

Doors are checked as wall apertures before any map is written. The text maps
are the runtime assets. This file records their original design coordinates.
"""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
SOLID = '12345678BL~'
OPEN = '.PKHFATMGgSsOoVvCcYyWwQq^&%=><}{DRZ'


class Map:
    def __init__(self, number, name, theme, w, h, stone, colors, start='N'):
        self.number, self.name, self.theme = number, name, theme
        self.w, self.h, self.stone, self.colors, self.start = w, h, stone, colors, start
        self.g = [[stone] * w for _ in range(h)]
        self.z = [[0.0] * w for _ in range(h)]
        self.c = [[None] * w for _ in range(h)]
        self.design, self.story, self.doors = [], [], []

    def box(self, name, x1, y1, x2, y2, cut=0, ceiling=1.5):
        self.design.append(f'; {name}: {x1},{y1} to {x2},{y2}')
        for y in range(y1, y2 + 1):
            for x in range(x1, x2 + 1):
                if min(x-x1, x2-x) + min(y-y1, y2-y) >= cut:
                    self.g[y][x] = '.'
                    self.c[y][x] = ceiling

    def route(self, name, *points, ceiling=1.125):
        self.design.append('; ' + name + ': ' + ' -> '.join(f'{x},{y}' for x, y in points))
        for (x, y), (xx, yy) in zip(points, points[1:]):
            assert x == xx or y == yy, (name, 'diagonal corridor')
            dx, dy = (xx > x)-(xx < x), (yy > y)-(yy < y)
            while True:
                # Existing rooms keep their shape and ceiling at the junction.
                if self.g[y][x] == self.stone:
                    self.g[y][x] = '.'
                    self.c[y][x] = ceiling
                if (x, y) == (xx, yy):
                    break
                x, y = x + dx, y + dy

    def wall(self, x, y, kind=None):
        assert self.g[y][x] in ('.', self.stone), (self.number, x, y)
        self.g[y][x] = kind or self.stone

    def floor(self, x1, y1, x2, y2, height):
        for y in range(y1, y2 + 1):
            for x in range(x1, x2 + 1):
                self.z[y][x] = height

    def roof(self, x1, y1, x2, y2, height):
        for y in range(y1, y2 + 1):
            for x in range(x1, x2 + 1):
                self.c[y][x] = height

    def steps(self, x, y, dx, dy, heights):
        for height in heights:
            assert self.g[y][x] == '.', (self.number, 'stair tread', x, y)
            self.z[y][x] = height
            x, y = x + dx, y + dy

    def thing(self, kind, *points):
        for x, y in points:
            assert self.g[y][x] == '.', (self.number, 'object overlap', kind, x, y, self.g[y][x])
            self.g[y][x] = kind

    def door(self, x, y, axis, kind='D'):
        self.thing(kind, (x, y))
        self.doors.append((x, y, axis, kind))

    def haven(self, x, y):
        self.story.append(f'@haven {x} {y} 1.15')

    def note(self, x, y, text):
        self.story.append(f'@note {x} {y} {text}')

    def scene(self, text):
        self.story.append('@scene ' + text)

    def trace(self, kind, *points):
        self.story.extend(f'@trace {kind} {x} {y}' for x, y in points)

    def regions(self, values, directive):
        seen = set()
        for y in range(self.h):
            for x in range(self.w):
                value = values[y][x]
                if value is None or (directive == 'height' and value == 0) or (x, y) in seen:
                    continue
                right = x
                while right+1 < self.w and values[y][right+1] == value and (right+1, y) not in seen:
                    right += 1
                bottom = y
                while bottom+1 < self.h and all(values[bottom+1][xx] == value and (xx, bottom+1) not in seen
                                              for xx in range(x, right+1)):
                    bottom += 1
                seen.update((xx, yy) for yy in range(y, bottom+1) for xx in range(x, right+1))
                yield f'@{directive} {x} {y} {right-x+1} {bottom-y+1} {value:g}'

    def write(self):
        for x, y, axis, kind in self.doors:
            dx, dy = (1, 0) if axis == 'EW' else (0, 1)
            assert all(self.g[y+s*dx][x+s*dy] in SOLID for s in [-1, 1]), (self.number, 'missing jamb', x, y)
            assert all(self.g[y+s*dy][x+s*dx] in OPEN for s in [-1, 1]), (self.number, 'blocked approach', x, y)
            assert all(abs(self.z[y][x] - self.z[y+s*dy][x+s*dx]) <= .125 for s in [-1, 1]), (self.number, 'door step', x, y)
            self.c[y][x] = self.z[y][x] + 1
        for y in range(self.h):
            for x in range(self.w):
                if self.c[y][x] is not None:
                    assert self.c[y][x] >= self.z[y][x] + 1, (self.number, 'headroom', x, y)
        for kind, count in [('P', 1), ('K', 3), ('X', 1), ('8', 2)]:
            assert sum(row.count(kind) for row in self.g) == count, (self.number, kind)
        for line in self.story:
            fields = line.split()
            if fields[0] in ('@haven', '@note'):
                points = [(int(fields[1]), int(fields[2]))]
            elif fields[0] == '@trace':
                points = [(int(fields[2]), int(fields[3]))]
            else:
                points = [(int(fields[2]), int(fields[3])), (int(fields[4]), int(fields[5]))]
                if fields[1] == 'armor':points.append((int(fields[7]), int(fields[8])))
            for x, y in points:
                assert self.g[y][x] in '.PKHFATMGgSsOoVvCcYyWwQq', (self.number, 'story on non-dry floor', line)
        text = '\n'.join([
            '; FORTRESS: authored maze, rooms and routes in tools/author_campaign.py.',
            f'@name {self.name}', f'@surfaces {self.theme}', f'@floor {self.colors[0]}', f'@ceil {self.colors[1]}',
            f'@start {self.start}', f'@horror {self.number}', *self.design, *self.story,
            *getattr(self, 'world', []),
            *self.regions(self.z, 'height'), *self.regions(self.c, 'ceiling'),
            *(''.join(row) for row in self.g), '',
        ])
        (ROOT / 'levels' / f'{self.number}.txt').write_text(text)
        print(f'Floor {self.number}: {self.w}x{self.h}, {len(text.encode())} bytes')


def guardhouse():
    m = Map(1, 'GUARDHOUSE', 'guardhouse', 39, 33, '1', ('#4a4038', '#1e1914'))
    m.box('Prison entry and visible exit', 2, 26, 6, 30, ceiling=1.25)
    m.box('Cell seen through the entry bars; reached on the return loop', 8, 27, 12, 29, ceiling=1.25)
    m.box('Guard mess at the first three-way junction', 9, 15, 13, 19, cut=1, ceiling=1.625)
    m.box('Western vestry', 2, 11, 6, 15, cut=1, ceiling=1.75)
    m.box('Raised watch chapel, first key', 3, 3, 9, 7, cut=1, ceiling=2)
    m.box('Bevelled nave; barred preview of the upper key gallery', 19, 5, 25, 11, cut=2, ceiling=2.25)
    m.box('Upper gallery, second key', 27, 4, 33, 9, cut=1, ceiling=2)
    m.box('Eastern kennel chapel, third key', 27, 22, 33, 27, cut=1, ceiling=1.75)
    m.box('Western concealed treasury', 1, 18, 3, 20, ceiling=1.25)
    m.box('Upper concealed treasury', 28, 1, 31, 2, ceiling=2)

    m.route('The first blind approach', (4,26), (4,22), (10,22), (10,19))
    m.route('West branch', (9,17), (4,17), (4,15))
    m.route('West chapel stair', (5,11), (5,7), ceiling=1.75)
    m.route('North branch into the nave', (11,15), (11,13), (15,13), (15,8), (19,8))
    m.route('Watch chapel return along the upper north passage', (9,5), (13,5), (13,2), (21,2), (21,5), ceiling=1.75)
    m.route('Gallery approach goes below its preview window', (22,11), (22,13), (26,13), (26,17), (31,17), (31,9), ceiling=1.75)
    m.route('Gallery returns down the outer eastern stair', (33,6), (36,6), (36,20), (30,20), (30,22), ceiling=1.75)
    m.route('Lower key return with two blind turns', (27,24), (22,24), (22,20), (17,20), (17,17), (13,17))
    m.route('Return through the cell glimpsed at the beginning', (17,20), (17,26), (12,26), (12,28))
    m.route('The cell rejoins the entry from below', (10,29), (10,31), (6,31), (6,30))
    m.route('Gate approach', (2,28), (0,28))
    m.route('Treasury approach', (5,17), (5,19), (3,19))
    m.route('Upper treasury aperture', (29,4), (29,2))
    m.route('Western breakable return', (6,13), (11,13))
    m.route('Inner gallery breach', (26,17), (22,17), (22,20))

    m.floor(3,3,9,7,.5); m.floor(10,5,13,5,.5); m.floor(13,2,13,4,.5)
    m.steps(5,8,0,1,[.375,.25,.125,0])
    m.steps(14,2,1,0,[.375,.25,.125,0])
    m.floor(27,4,33,9,.5); m.floor(28,1,31,3,.5); m.floor(26,5,26,5,.5)
    m.steps(31,10,0,1,[.375,.25,.125,0])
    m.floor(34,6,36,6,.5); m.floor(36,7,36,10,.5)
    m.steps(36,11,0,1,[.375,.25,.125,0])
    m.floor(21,8,23,9,-.125)
    m.roof(19,5,20,11,1.5); m.roof(24,5,25,11,1.5)
    m.wall(20,7,'2'); m.wall(24,9,'2')
    m.wall(28,23,'2'); m.wall(32,24,'2')
    m.wall(7,28,'B'); m.wall(26,7,'B'); m.roof(26,7,26,7,2.25)
    m.wall(6,19,'L'); m.wall(26,5,'L')
    for x,y,axis,kind in [(4,25,'NS','D'), (8,17,'EW','D'), (5,8,'NS','D'),
                          (10,5,'EW','D'), (21,4,'NS','R'), (11,14,'NS','D'),
                          (22,12,'NS','D'), (31,10,'NS','D'), (34,6,'EW','D'),
                          (30,21,'NS','D'), (26,24,'EW','D'), (14,17,'EW','D'),
                          (10,30,'NS','D'), (4,19,'EW','Z'), (29,3,'NS','Z')]:
        m.door(x,y,axis,kind)
    m.thing('8',(8,13),(25,17)); m.thing('X',(0,28)); m.thing('P',(3,29))
    m.thing('K',(5,4),(30,6),(32,25))
    m.thing('=',(22,8),(21,9),(22,9),(23,9)); m.thing('>',(21,8),(23,8))
    m.thing('!',(28,25)); m.thing('&',(17,8)); m.roof(17,8,17,8,1)
    m.thing('^',(7,5)); m.thing('M',(2,19)); m.thing('T',(3,18),(30,1),(31,2))
    m.thing('G',(12,18),(29,24)); m.thing('c',(30,24)); m.thing('S',(6,4))
    m.thing('v',(22,7),(3,6)); m.thing('w',(32,8)); m.thing('Y',(11,29)); m.thing('y',(12,28))
    m.thing('Q',(11,16),(32,23)); m.thing('q',(3,13),(28,7))
    m.thing('A',(4,29),(10,16),(23,6),(8,6),(32,5),(31,26),(10,27))
    m.thing('H',(2,27),(4,12),(21,10),(28,26)); m.thing('F',(6,14),(9,29))
    m.haven(3,28); m.haven(20,9)
    m.note(3,27,'Keep to the white candles. The Jailer cannot cross their light.|If you hear chains, walk quietly and break his line of sight. A bolt will hold him for a moment. He cannot be killed.')
    m.note(20,10,'The man behind the bars has stood there for nine nights. He does not eat. He does not turn.|This morning his cell was empty. The lock was still sealed. Tonight I heard him behind me.')
    m.scene('cell 5 28 9 28 0'); m.scene('shadow 15 8 19 8 0')
    m.scene('knock 27 24 32 26 0'); m.scene('armor 21 11 24 8 1 23 10')
    m.trace('blood',(4,24),(4,23),(30,25)); m.trace('scratch',(3,27),(20,10)); m.trace('chains',(15,9))
    return m


def cellars():
    m = Map(2, 'THE CELLARS', 'cellars', 41, 35, '3', ('#453a2c', '#1a140e'), 'E')
    m.box('Intake guardroom and exit',2,2,6,5,ceiling=1.25)
    m.box('Valve antechamber',5,9,9,13,cut=1,ceiling=1.5)
    m.box('Upper inspection lodge',14,2,18,5,cut=1,ceiling=1.375)
    m.box('Deep crypt, first key',2,22,8,27,cut=2,ceiling=1.25)
    m.box('Cistern with sunken water and raised crossing',16,11,24,20,cut=2,ceiling=1.875)
    m.box('Northern pump house, second key',29,3,35,9,cut=1,ceiling=2)
    m.box('Cold store, third key',29,22,36,28,cut=2,ceiling=1.25)
    m.box('Crypt treasury',2,30,4,32,ceiling=1)
    m.box('Northern concealed cache',26,1,28,2,ceiling=1.25)
    m.route('Intake bends before the first encounter',(6,4),(11,4),(11,8),(8,8),(8,9))
    m.route('First fork through the inspection lodge',(11,4),(14,4))
    m.route('Lodge to the northern service passage',(18,4),(21,4),(21,7),(25,7),(25,5),(29,5))
    m.route('Barred inspection recess',(16,7),(20,7))
    m.route('Crypt descent',(6,13),(6,17),(5,17),(5,22))
    m.route('Cistern descent turns before revealing the water',(9,11),(13,11),(13,15),(16,15))
    m.route('Cistern north stair to the pump route',(20,11),(20,7))
    m.route('Eastern dry drain below the pump house',(24,17),(27,17),(27,21),(31,21),(31,22))
    m.route('Drain window faces the cistern',(26,15),(27,15),(27,17))
    m.route('Lower circuit through the cold store',(29,26),(25,26),(25,30),(17,30),(17,25),(13,25),(13,20),(8,20),(8,24))
    m.route('Outer pump stair returns to the cold store',(35,7),(38,7),(38,16),(34,16),(34,22))
    m.route('Crypt cache approach',(6,27),(9,27),(9,31),(4,31))
    m.route('Northern cache approach',(27,5),(27,2))
    m.route('Broken western drain shortcut',(13,20),(13,17),(16,17))
    m.route('Breakable upper maintenance loop',(16,2),(16,1),(24,1),(24,5),(25,5))
    m.route('Exit throat',(2,3),(0,3))
    m.floor(2,22,9,32,-.5);m.floor(16,11,24,20,-.5)
    m.steps(5,18,0,1,[-.125,-.25,-.375,-.5])
    m.steps(13,12,0,1,[-.125,-.25,-.375,-.5]);m.floor(14,15,15,15,-.5)
    m.steps(20,10,0,-1,[-.375,-.25,-.125,0])
    for x1,y1,x2,y2 in [(25,26,28,26),(25,27,25,30),(17,30,24,30),(17,25,17,29),
                           (13,25,16,25),(13,17,13,24),(8,20,12,20),(8,21,8,23),
                           (25,17,27,17),(27,15,27,21),(28,21,31,21),(31,21,31,22),
                           (34,16,38,16),(34,17,34,21),(38,14,38,15),(26,15,26,15),
                           (14,17,15,17)]:m.floor(x1,y1,x2,y2,-.5)
    m.floor(29,22,36,28,-.5)
    m.steps(38,10,0,1,[-.125,-.25,-.375,-.5])
    m.floor(18,13,22,18,-.625);m.floor(20,13,20,18,-.5);m.floor(18,15,22,15,-.5)
    m.roof(16,11,17,20,1.125);m.roof(23,11,24,20,1.125)
    m.floor(6,30,6,30,-.5)
    m.wall(16,6,'B');m.wall(25,15,'B');m.floor(25,15,25,15,-.5);m.roof(25,15,25,15,1.875)
    for x,y in [(3,24),(7,25),(17,12),(23,12),(17,19),(23,19),(30,4),(34,8),(30,24),(35,25)]:m.wall(x,y,'4')
    # Offset masonry islands break the cistern into bays around the crossing.
    for x1,y1,x2,y2 in [(18,13,19,14),(21,17,22,18)]:
        for y in range(y1,y2+1):
            for x in range(x1,x2+1):m.wall(x,y,'4')
    m.wall(6,30,'L');m.wall(26,4,'L')
    for x,y,axis,kind in [(7,4,'EW','D'),(13,4,'EW','D'),(19,4,'EW','D'),(5,21,'NS','D'),
                           (15,15,'EW','D'),(20,10,'NS','R'),(28,5,'EW','D'),(36,7,'EW','D'),
                           (27,20,'NS','D'),(28,26,'EW','D'),(13,23,'NS','D'),(8,21,'NS','D'),
                           (34,20,'NS','D'),(5,31,'EW','Z'),(27,3,'NS','Z')]:m.door(x,y,axis,kind)
    m.thing('8',(14,17),(21,1));m.thing('P',(3,4));m.thing('X',(0,3))
    m.thing('K',(5,25),(33,6),(34,26))
    for y in range(13,19):
        for x in range(18,23):
            if x==20 or y==15 or m.g[y][x]!='.':continue
            m.thing('>' if y==16 else '=',(x,y))
    m.thing('!',(6,23));m.thing('%',(27,18));m.roof(27,18,27,18,.5)
    m.thing('^',(31,5));m.thing('M',(3,31));m.thing('T',(3,30),(26,1),(28,2))
    m.thing('G',(8,11));m.thing('c',(7,11));m.thing('S',(33,5))
    m.thing('V',(21,12));m.thing('v',(3,25));m.thing('Y',(7,26));m.thing('y',(5,26));m.thing('W',(34,25))
    m.thing('Q',(8,12),(31,7));m.thing('q',(6,11),(34,24))
    m.thing('A',(5,4),(6,10),(17,7),(3,23),(22,19),(30,6),(32,27),(9,28))
    m.thing('H',(4,2),(17,4),(4,24),(31,26));m.thing('F',(8,10),(30,8),(35,26))
    m.haven(3,3);m.haven(16,3)
    m.note(4,3,'The knocking comes from the old cistern. We drained it yesterday. There is nothing beneath the grating.|When I knock twice, it knocks three times. I have stopped counting.')
    m.note(17,4,'We buried the first keeper below the cellars, his keys chained to his wrist.|The lord ordered every gate locked. By morning the keys were hanging outside his own chamber.')
    m.scene('knock 8 8 8 11 0');m.scene('shadow 14 15 17 15 0');m.scene('armor 31 22 30 25 1 35 26')
    m.trace('blood',(6,16),(17,16));m.trace('scratch',(4,3),(17,4));m.trace('chains',(14,15),(30,26))
    return m


def barracks():
    m=Map(3,'BARRACKS','barracks',43,35,'2',('#45443c','#191a16'),'W')
    m.box('Southern watch entrance',34,28,39,32,ceiling=1.25)
    m.box('Mess and first choice of route',23,28,27,31,ceiling=1.5)
    m.box('Bathing room below the barracks',14,27,18,31,cut=1,ceiling=1.375)
    m.box('Lower armoury, first key',3,24,8,29,cut=1,ceiling=1.875)
    m.box('Northwest sleeping ward',3,3,8,8,cut=1,ceiling=1.5)
    m.box('Exercise hall below the gallery',20,12,25,17,cut=1,ceiling=2.25)
    m.box('Captains upper quarters, second key',32,3,38,8,cut=1,ceiling=2.5)
    m.box('Raised private chapel, third key',20,20,25,25,cut=1,ceiling=2)
    m.box('Northern concealed weapons',14,1,17,3,ceiling=1.25)
    m.box('Southern concealed pay chest',30,23,32,25,ceiling=1.25)
    m.route('Entry chooses the mess or outer stair',(34,30),(27,30))
    m.route('Eastern barrack passage has four blind turns',(36,28),(36,25),(39,25),(39,20),(34,20),(34,16),(30,16),(30,15),(34,15),(34,8),ceiling=2)
    m.route('Mess chapel approach',(25,28),(25,25),ceiling=1.5)
    m.route('Chapel descent into the exercise hall',(23,20),(23,17),ceiling=1.625)
    m.route('The low exercise hall connects the two circuits',(25,15),(30,15))
    m.route('Westward circuit through the lower armoury',(20,15),(17,15),(17,20),(12,20),(12,25),(8,25))
    m.route('Armoury north route through the sleeping ward',(5,24),(5,20),(3,20),(3,15),(8,15),(8,8))
    m.route('Upper gallery stair returns to the sleeping ward',(8,5),(12,5),(12,10),(21,10),(27,10),(27,5),(32,5),ceiling=2)
    m.route('Outer captain stair returns to the entrance route',(38,5),(40,5),(40,15),(38,15),(38,18),(34,18),ceiling=2)
    m.route('Lower bath loop',(23,30),(18,30))
    m.route('Bath west return',(14,29),(10,29),(10,25),(12,25))
    m.route('Northern cache aperture',(12,5),(15,5),(15,3))
    m.route('Southern cache aperture',(31,30),(31,25))
    m.route('Breakable lower hall return',(17,17),(20,17))
    m.route('Breakable chapel service passage',(24,20),(30,20),(30,16))
    m.route('Exit throat',(39,30),(42,30))
    m.floor(32,3,38,8,.75);m.floor(28,5,31,5,.75);m.floor(27,5,27,10,.75);m.floor(21,10,26,10,.75)
    m.steps(20,10,-1,0,[.625,.5,.375,.25,.125,0]);m.steps(34,9,0,1,[.625,.5,.375,.25,.125,0])
    m.floor(39,5,40,5,.75);m.floor(40,6,40,9,.75);m.steps(40,10,0,1,[.625,.5,.375,.25,.125,0])
    m.floor(20,20,25,25,.25);m.steps(25,26,0,1,[.125,0]);m.steps(23,19,0,-1,[.125,0]);m.steps(26,20,1,0,[.125,0])
    m.roof(25,20,27,20,1.625)
    m.floor(14,27,18,31,-.125);m.floor(12,29,13,29,-.125)
    for x,y in [(4,25),(7,28),(21,21),(24,24)]:m.wall(x,y,'4')
    for x in [21,22]:
        for y in [14,15]:m.wall(x,y,'4')
    m.roof(20,12,20,17,1.5);m.roof(25,12,25,17,1.5)
    m.wall(22,11,'B');m.roof(22,11,22,11,2.25)
    m.wall(13,4,'L');m.wall(30,28,'L')
    for x,y,axis,kind in [(33,30,'EW','D'),(36,27,'NS','D'),(25,27,'NS','D'),(23,19,'NS','D'),
                           (26,15,'EW','D'),(9,25,'EW','D'),(5,23,'NS','D'),(8,13,'NS','D'),(8,9,'NS','D'),
                           (9,5,'EW','D'),(31,5,'EW','D'),(34,9,'NS','D'),(39,5,'EW','D'),
                           (19,30,'EW','R'),(13,29,'EW','D'),(15,4,'NS','Z'),(31,26,'NS','Z')]:m.door(x,y,axis,kind)
    m.thing('8',(19,17),(28,20));m.thing('P',(37,30));m.thing('X',(42,30))
    m.thing('K',(5,27),(36,5),(22,23))
    for x in [15,16,17]:
        m.thing('=',(x,28));m.thing('>' if x!=16 else '=',(x,29))
    m.thing('!',(7,25));m.thing('&',(24,10));m.roof(24,10,24,10,1.75);m.thing('^',(35,6))
    m.thing('M',(16,2));m.thing('T',(14,1),(17,3),(30,24),(32,25))
    m.wall(2,17,'~');m.wall(28,8,'~');m.floor(28,8,28,8,.75);m.roof(28,8,28,8,2)
    m.thing('G',(25,30),(6,5));m.thing('S',(5,26),(35,5));m.thing('c',(6,26))
    m.thing('V',(23,16));m.thing('w',(38,7));m.thing('Y',(16,27));m.thing('y',(18,29))
    m.thing('Q',(7,27),(33,7));m.thing('q',(4,27),(24,14))
    m.thing('A',(38,31),(24,29),(4,26),(7,7),(23,13),(33,5),(21,24),(17,31))
    m.thing('H',(35,29),(4,28),(37,6),(23,24));m.thing('F',(6,6),(17,30))
    m.haven(36,30);m.haven(15,30)
    m.note(37,31,'The captain had the empty suits placed facing the wall. Each dawn they faced our bunks.|Now there are fewer men at roll call, and more suits of armour. Nobody asks where they came from.')
    m.note(16,30,'Our oath was to keep the prisoners here until the last gate fell.|The keeper died with those words in his mouth. The oath did not die with him. Every stolen key wakes it again.')
    m.scene('armor 25 29 24 31 0 26 31');m.scene('shadow 17 19 20 16 0');m.scene('mirror 26 10 27 8 1')
    m.trace('blood',(36,26),(39,23));m.trace('scratch',(37,31),(27,9));m.trace('chains',(16,30))
    return m


def vault():
    m=Map(4,'THE VAULT','vault',41,35,'4',('#3c3f42','#15171a'),'W')
    m.box('Eastern receiving cell and exit',32,18,37,22,ceiling=1.375)
    m.box('Western flooded crypt, first key',3,7,8,12,cut=1,ceiling=1.625)
    m.box('Deep burial stack, second key',6,26,12,31,cut=1,ceiling=1.125)
    m.box('Upper northeastern reliquary, third key',29,5,36,10,cut=1,ceiling=2.125)
    m.box('Octagonal ossuary around an opaque central tomb',15,15,23,23,cut=3,ceiling=2)
    m.box('Northern candle scriptorium',17,1,21,3,ceiling=1.375)
    m.box('Western concealed treasury',2,2,4,4,ceiling=1.25)
    m.box('Southern concealed treasury',24,30,27,32,ceiling=1.125)
    m.route('Northern spiral approach with a choice at the reliquary stair',(34,18),(34,14),(27,14),(27,11),(25,11),(25,7),(20,7),(20,4),(15,4),(15,9),(8,9))
    m.route('Upper reliquary stair is reached around the western bend',(25,7),(29,7),ceiling=1.625)
    m.route('Low preview recess beneath the reliquary window',(34,14),(32,14),(32,12))
    m.route('Receiving-cell lower descent',(32,20),(28,20),(28,25),(23,25),(23,22))
    m.route('Ossuary north return',(19,15),(19,13),(15,13),(15,9))
    m.route('Western crypt descent into the lowest burial stack',(3,10),(1,10),(1,17),(6,17),(6,22),(9,22),(9,26))
    m.route('Burial stack rises to the ossuary from the south',(12,29),(16,29),(16,25),(18,25),(18,23))
    m.route('Reliquary outer stair rejoins the receiving-cell passage',(36,7),(38,7),(38,13),(36,13),(36,15),(34,15),ceiling=1.625)
    m.route('Candle scriptorium approach',(20,4),(20,3))
    m.route('Western cache approach',(11,9),(11,6),(10,6),(10,3),(4,3))
    m.route('Southern cache approach',(23,25),(23,28),(25,28),(25,30))
    m.route('Western ossuary breach through the burial partition',(6,19),(10,19),(10,18),(15,18))
    m.route('Eastern ossuary breach and short service stair',(23,18),(27,18),(27,14))
    m.route('Exit throat',(37,20),(40,20))
    m.floor(3,7,8,12,-.25);m.floor(2,10,2,10,-.25);m.steps(9,9,1,0,[-.125,0])
    m.floor(1,10,1,17,-.25);m.floor(2,17,6,17,-.25);m.floor(6,18,6,22,-.25);m.floor(7,22,8,22,-.25)
    m.steps(9,22,0,1,[-.375,-.5,-.625,-.75]);m.floor(6,26,12,31,-.75)
    m.floor(13,29,16,29,-.75);m.steps(16,28,0,-1,[-.625,-.5,-.375,-.25]);m.floor(17,25,18,25,-.25);m.floor(18,24,18,24,-.25)
    m.floor(15,15,23,23,-.25);m.steps(19,14,0,-1,[-.125,0])
    m.steps(28,23,0,1,[-.125,-.25]);m.floor(23,25,28,25,-.25);m.floor(23,22,23,24,-.25)
    m.floor(29,5,36,10,.25);m.steps(28,7,-1,0,[.125,0])
    m.floor(37,7,38,7,.25);m.floor(38,8,38,8,.25);m.steps(38,9,0,1,[.125,0])
    m.floor(23,26,23,28,-.25);m.floor(24,28,25,28,-.25);m.floor(25,29,25,29,-.25);m.floor(24,30,27,32,-.25);m.floor(26,28,26,28,-.25)
    m.floor(7,19,10,19,-.25);m.floor(10,18,14,18,-.25);m.floor(24,18,27,18,-.25);m.steps(27,17,0,-1,[-.125,0])
    m.floor(4,9,6,10,-.375)
    for x in range(18,21):
        for y in range(18,21):m.wall(x,y,'5')
    for x,y in [(4,8),(7,11),(7,27),(11,30),(31,6),(34,9),(16,17),(22,21)]:m.wall(x,y,'5')
    m.roof(15,15,16,23,1.25);m.roof(22,15,23,23,1.25)
    m.wall(6,2,'L');m.wall(26,28,'L');m.wall(32,11,'B');m.roof(32,11,32,11,2.125)
    for x,y,axis,kind in [(34,17,'NS','D'),(31,20,'EW','D'),(9,9,'EW','D'),(2,10,'EW','D'),
                           (9,25,'NS','D'),(13,29,'EW','D'),(18,24,'NS','D'),(19,14,'NS','R'),
                           (28,24,'NS','D'),(28,7,'EW','D'),(37,7,'EW','D'),
                           (5,3,'EW','Z'),(25,29,'NS','Z')]:m.door(x,y,axis,kind)
    m.thing('8',(14,18),(24,18));m.thing('P',(35,20));m.thing('X',(40,20))
    m.thing('K',(5,11),(10,29),(34,7))
    for y in [9,10]:
        for x in [4,5,6]:m.thing('>' if y==9 else '=',(x,y))
    m.thing('!',(17,21));m.thing('%',(1,14));m.roof(1,14,1,14,.75);m.thing('^',(35,8))
    m.thing('M',(3,3));m.thing('T',(2,2),(4,4),(24,31),(27,32))
    m.thing('S',(4,11),(8,30));m.thing('G',(35,6));m.thing('V',(22,17));m.thing('w',(11,29))
    m.thing('Y',(8,28));m.thing('y',(10,27))
    m.thing('Q',(7,9),(31,8));m.thing('q',(11,28),(21,16))
    m.thing('A',(36,19),(6,8),(10,6),(21,2),(22,19),(8,27),(9,30),(30,6))
    m.thing('H',(33,19),(3,10),(17,2),(10,30),(35,9));m.thing('F',(21,22),(7,28))
    m.haven(35,21);m.haven(18,2)
    m.note(36,21,'There is no gold in the oldest vault. Only names cut into the stone.|I found mine among them. The date beside it is tomorrow. The scratch marks are still warm.')
    m.note(19,2,'Every key passed from a dead keeper to a living soldier. The gates stayed locked.|The oath binds the whole estate. Open every gate, then carry the last keys beyond the walls.')
    m.scene('armor 34 19 33 21 1 36 21');m.scene('shadow 15 13 19 16 0');m.scene('knock 9 27 11 29 1')
    m.trace('blood',(34,16),(34,15));m.trace('scratch',(36,21),(19,2));m.trace('chains',(1,16),(21,21))
    return m


def keep():
    m=Map(5,"WARLORD'S KEEP",'keep',45,37,'5',('#4a3a30','#201410'),'N')
    m.box('Southwestern watch room',3,28,8,33,ceiling=1.375)
    m.box('Sunken cloister garden',11,27,15,32,cut=1,ceiling=1.5)
    m.box('Western chapel, first key',3,5,9,10,cut=2,ceiling=2.125)
    m.box('Raised throne nave with narrow side aisles',20,12,27,20,cut=2,ceiling=2.75)
    m.box('Upper apse and final gate approach',21,4,26,8,cut=1,ceiling=3)
    m.box('Eastern knights armoury, second key',33,25,40,31,cut=2,ceiling=1.875)
    m.box('Northern tower, third key',33,3,39,8,cut=1,ceiling=2.5)
    m.box('Hidden chapel reliquary',3,1,6,2,ceiling=1.75)
    m.box('Hidden royal pay chest',36,33,39,35,ceiling=1.25)
    m.route('Entry cloister and lower nave approach',(8,31),(12,31),(12,29),(18,29),(18,24),(24,24),(24,20),ceiling=1.875)
    m.route('Western chapel approach with three blind corners',(5,28),(5,23),(9,23),(9,18),(4,18),(4,14),(6,14),(6,10),ceiling=1.5)
    m.route('Chapel returns on the upper western stair',(9,7),(13,7),(13,3),(18,3),(18,14),(20,14),ceiling=1.875)
    m.route('Preview recess looking through to the high apse',(18,6),(19,6),ceiling=1.625)
    m.route('The nave climbs into the apse',(23,12),(23,8),ceiling=2.125)
    m.route('Exit throat behind the throne',(24,4),(24,0),ceiling=2.5)
    m.route('Lower eastern armoury approach',(24,24),(28,24),(28,28),(33,28),ceiling=1.5)
    m.route('Nave east stair rejoins the armoury corridor',(27,17),(31,17),(31,20),(34,20),(34,23),(37,23),(37,25),ceiling=1.875)
    m.route('Outer eastern route to the tower stair',(37,25),(37,21),(41,21),(41,15),(37,15),(36,15),(36,8),ceiling=1.875)
    m.route('Tower west return overlooks the nave',(33,5),(30,5),(30,9),(26,9),(26,12),ceiling=1.875)
    m.route('Western middle link gives a second chapel return',(9,18),(13,18),(13,23),(18,23),(18,29),ceiling=1.375)
    m.route('Chapel reliquary aperture',(5,5),(5,2),ceiling=1.75)
    m.route('Armoury pay chest aperture',(37,31),(37,33))
    m.route('Breakable western stair to the nave',(13,20),(16,20),(16,18),(20,18),ceiling=1.875)
    m.route('Breakable eastern descent to the tower stair',(27,14),(36,14),ceiling=1.875)
    m.floor(11,27,15,32,-.125)
    m.floor(3,5,9,10,.25);m.steps(6,11,0,1,[.125,0])
    m.floor(10,7,13,7,.25);m.floor(13,3,13,6,.25);m.floor(14,3,18,3,.25);m.floor(18,4,18,9,.25)
    m.steps(18,10,0,1,[.375,.5]);m.floor(18,12,18,14,.5);m.floor(19,14,19,14,.5);m.floor(19,6,20,6,.25)
    m.floor(20,12,27,20,.5);m.steps(24,21,0,1,[.375,.25,.125,0])
    m.floor(21,4,26,8,1);m.floor(24,0,24,3,1);m.steps(23,9,0,1,[.875,.75,.625,.5])
    m.floor(28,17,31,17,.5);m.steps(31,18,0,1,[.375,.25,.125])
    m.floor(33,3,39,8,.75);m.steps(36,9,0,1,[.625,.5,.375,.25,.125,0])
    m.floor(30,5,32,5,.75);m.floor(30,6,30,7,.75);m.steps(30,8,0,1,[.625,.5]);m.floor(26,9,29,9,.5);m.floor(26,10,26,12,.5)
    m.floor(3,1,6,2,.25);m.floor(5,3,5,4,.25);m.floor(7,4,7,4,.25)
    m.steps(16,18,1,0,[.125,.25,.375,.5]);m.floor(28,14,31,14,.5);m.steps(32,14,1,0,[.375,.25,.125,0])
    for x,y in [(4,6),(8,9),(21,14),(26,18),(22,18),(25,14),(35,26),(38,30),(11,30),(14,31)]:m.wall(x,y,'1')
    for x in [23,24]:
        for y in [5,6]:m.wall(x,y,'6')
    m.roof(20,12,21,20,1.875);m.roof(26,12,27,20,1.875);m.roof(22,16,25,16,2.25)
    m.wall(20,6,'B');m.roof(20,6,20,6,2.75);m.wall(7,4,'L');m.wall(35,32,'L')
    for x,y,axis,kind in [(9,31,'EW','D'),(5,27,'NS','D'),(6,11,'NS','D'),(10,7,'EW','D'),
                           (19,14,'EW','D'),(24,21,'NS','R'),(23,9,'NS','D'),(32,28,'EW','D'),
                           (28,17,'EW','D'),(37,24,'NS','D'),(36,9,'NS','D'),(32,5,'EW','D'),
                           (26,11,'NS','D'),(12,18,'EW','D'),(16,23,'EW','D'),(5,3,'NS','Z'),(37,32,'NS','Z')]:m.door(x,y,axis,kind)
    m.thing('8',(19,18),(31,14));m.thing('P',(4,32));m.thing('X',(24,0))
    m.thing('K',(5,7),(37,28),(37,5))
    for y in [28,29]:
        for x in [12,13,14]:m.thing('>' if y==29 else '=',(x,y))
    m.thing('!',(7,6));m.thing('%',(41,18));m.roof(41,18,41,18,1);m.thing('^',(38,28))
    m.thing('M',(4,1));m.thing('T',(3,2),(6,2),(36,34),(39,35))
    m.wall(10,20,'~');m.wall(31,8,'~');m.floor(31,8,31,8,.625);m.roof(31,8,31,8,1.875)
    m.thing('S',(6,7),(36,27));m.thing('O',(37,4));m.thing('c',(36,28))
    m.thing('v',(24,16));m.thing('w',(38,7));m.thing('Y',(14,30));m.thing('y',(13,31))
    m.thing('Q',(7,8),(34,29));m.thing('q',(35,5),(25,18))
    m.thing('A',(7,29),(24,19),(6,9),(25,7),(39,27),(34,6),(15,31),(14,23))
    m.thing('H',(3,30),(3,8),(36,29),(38,6));m.thing('F',(21,16),(26,19))
    m.haven(4,31);m.haven(34,30)
    m.note(5,31,"The lord's chamber is empty. His armour sits at the table. Something inside it is breathing.|I saw his face in the mirror. It was standing behind mine.")
    m.note(35,30,'This is the last gate. Take the keys beyond the walls and the oath will have no prison left to bind.|Do not stop for the voice behind you. Do not answer when it uses your name. Keep walking.')
    m.scene('mirror 7 30 7 32 0');m.scene('shadow 18 25 24 24 1');m.scene('armor 37 21 39 29 1 36 29');m.scene('mirror 30 9 30 7 1')
    m.trace('blood',(5,26),(5,25),(30,17),(37,23));m.trace('scratch',(5,31),(30,8));m.trace('chains',(35,30))
    return m


if __name__ == '__main__':
    from author_world import expand
    builders = {1: guardhouse, 2: cellars, 3: barracks, 4: vault, 5: keep}
    for n in map(int, sys.argv[1:] or builders):
        expand(builders[n]()).write()
