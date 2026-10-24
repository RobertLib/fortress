"""Hand-authored outdoor districts attached to the campaign's interior loops.

Imported by author_campaign.py. Coordinates are relative to the old eastern
wall, not random seeds. Indoor geometry and existing stairs remain authored
in author_campaign.py; every chapter requires its new district's key.
"""

GROUND = ['stone', 'grass', 'earth', 'cobble', 'gravel', 'leaves']


class District:
    def __init__(self, m):
        self.m, self.x = m, m.w
        m.w += 27
        for y in range(m.h):
            m.g[y].extend([m.stone] * 27)
            m.z[y].extend([0.] * 27)
            m.c[y].extend([None] * 27)
        self.sky = [[False] * m.w for _ in range(m.h)]
        self.ground = [[0] * m.w for _ in range(m.h)]
        self.lines, self.props = [], set()

    def court(self, name, x1, y1, x2, y2, ground='grass', cut=0):
        m, ox = self.m, self.x
        m.box(name, ox+x1, y1, ox+x2, y2, cut=cut, ceiling=1.75)
        m.roof(ox+x1-1, y1-1, ox+x2+1, y2+1, 1.75)
        for y in range(y1, y2+1):
            for x in range(ox+x1, ox+x2+1):
                if m.g[y][x] == '.': self.sky[y][x] = True
        self.surface(ground, x1, y1, x2, y2)

    def surface(self, kind, x1, y1, x2, y2):
        for y in range(y1, y2+1):
            for x in range(self.x+x1, self.x+x2+1): self.ground[y][x] = GROUND.index(kind)

    def building(self, name, x1, y1, x2, y2, door, axis='NS', wall='3', ceiling=2, roof_axis=1, roof_rise=1):
        m, ox = self.m, self.x
        for y in range(y1, y2+1):
            for x in range(ox+x1, ox+x2+1):
                m.g[y][x] = wall
                m.c[y][x] = ceiling
                self.sky[y][x] = False
                self.ground[y][x] = 0
        m.box(name, ox+x1+1, y1+1, ox+x2-1, y2-1, ceiling=ceiling)
        dx, dy = door
        m.g[dy][ox+dx] = '.'
        m.door(ox+dx, dy, axis)
        self.lines.append(f'@roof {ox+x1} {y1} {x2-x1+1} {y2-y1+1} {ceiling+.125} {min(4.875,ceiling+.125+roof_rise)} {roof_axis} {int(wall!="3")}')

    def connect(self, name, interior, exterior):
        ix, iy = interior; ex, ey = exterior
        assert iy == ey
        self.m.route(name, (ix, iy), (self.x+ex, ey), ceiling=1.375)
        # The breach through the fortress wall becomes an open arch, with
        # the sky visible immediately after the original perimeter.
        for x in range(self.x-1, self.x+ex+1):
            self.sky[ey][x] = True
            self.ground[ey][x] = GROUND.index('cobble')

    def prop(self, kind, *points):
        for x, y in points:
            assert self.m.g[y][self.x+x] == '.', (self.m.number, kind, x, y)
            assert (x, y) not in self.props
            self.props.add((x, y))
            self.lines.append(f'@prop {kind} {self.x+x} {y}')

    def thing(self, kind, *points): self.m.thing(kind, *((self.x+x, y) for x, y in points))

    def key(self, old, new):
        x, y = old
        assert self.m.g[y][x] == 'K'
        self.m.g[y][x] = 'A'
        self.thing('K', new)

    def monk(self, x, y, name, words):
        assert len(words) < 224
        assert self.m.g[y][self.x+x] == '.' and (x, y) not in self.props
        self.props.add((x, y))
        self.lines.append(f'@monk {self.x+x} {y} {name}|{words}')

    def haven(self, x, y): self.m.haven(self.x+x, y)
    def landmark(self, x, y, radius, name): self.lines.append(f'@landmark {self.x+x} {y} {radius} {name}')

    def finish(self):
        m = self.m
        # Windows are authored against genuine exterior faces and keep door
        # apertures clear. They are perspective-correct wall details.
        for roof in [line.split() for line in self.lines if line.startswith('@roof ')] :
            _,rx,ry,rw,rh,eave,_,_,_=roof
            rx,ry,rw,rh=int(rx),int(ry),int(rw),int(rh)
            kind='glass' if float(eave)>3 else 'shutter'
            faces=[]
            # Tall church windows flank the central portal symmetrically;
            # ordinary houses have a balanced pair of smaller shuttered windows.
            columns = range(rx+4,rx+rw-1,6) if kind=='glass' else sorted({rx+2,rx+rw-3})
            for x in columns:
                faces.extend([(x,ry,0,-1),(x,ry+rh-1,0,1)])
            for y in range(ry+2,ry+rh-1,3): faces.extend([(rx,y,-1,0),(rx+rw-1,y,1,0)])
            for x,y,dx,dy in faces:
                if not (0<x+dx<m.w-1 and 0<y+dy<m.h-1): continue
                if m.g[y][x] not in '123456' or m.g[y+dy][x+dx]!='.' or not self.sky[y+dy][x+dx]: continue
                if kind=='glass' and any(m.g[yy][xx] in 'DR' for yy in range(max(0,y-2),min(m.h,y+3)) for xx in range(max(0,x-2),min(m.w,x+3))): continue
                self.lines.append(f'@facade {x} {y} {dx} {dy} {kind}')
        lines = []
        sky = [[1 if cell else None for cell in row] for row in self.sky]
        for directive in m.regions(sky, 'outside'): lines.append(directive.rsplit(' ', 1)[0])
        ground = [[cell if cell or x >= self.x else None for x, cell in enumerate(row)] for row in self.ground]
        for directive in m.regions(ground, 'terrain'):
            _, x, y, w, h, value = directive.split()
            lines.append(f'@terrain {GROUND[int(value)]} {x} {y} {w} {h}')
        m.world = lines + self.lines
        # Outer enclosure is old mossy estate masonry. A forest boundary
        # must not inherit the vault's repeating barred prison windows.
        for row in m.g:
            for x in range(self.x,m.w):
                if row[x] == m.stone: row[x] = '2'
        # Authoring catches overlaps and unframed building doors before export.
        for x, y in self.props:
            assert m.g[y][self.x+x] == '.', (m.number, 'prop overlap', x, y)
        return m


def village(d):
    m = d.m; m.name = 'THE OUTER WARD'
    d.court('Village green and market square', 2, 12, 24, 29, cut=2)
    d.court('North lane around the inn', 6, 3, 24, 16, 'cobble', cut=1)
    d.surface('cobble', 2, 18, 24, 21)
    d.surface('earth', 10, 13, 13, 29)
    d.building('Abandoned cooper cottage', 3, 23, 9, 28, (6, 23), wall='3')
    d.building('Village inn and the innkeepers room', 14, 4, 22, 11, (18, 11), wall='3', ceiling=2.125,
               roof_axis=0, roof_rise=2.5)
    for x in range(15,22):
        if x != 18: m.wall(d.x+x,8,'3')
    m.door(d.x+18,8,'NS')
    d.building('Bakers cottage and bread store', 3, 13, 8, 17, (8, 15), 'EW', '3', 1.625)
    d.connect('First emergence into the village square', (36, 20), (2, 20))
    d.connect('North lane returns to the guardhouse stair', (36, 6), (6, 6))
    m.route('Abbey stair gate at the far end of the north lane', (d.x+24, 6), (m.w-1, 6))
    m.thing('X', (m.w-1, 6)); d.surface('cobble', 24, 6, 26, 6)
    for i, z in enumerate([.375, .25, .125, 0]): m.z[6][37+i] = z
    d.key((32, 25), (20, 6))
    d.prop('well', (12, 19)); d.prop('oak', (4, 19), (22, 25), (11, 14), (23, 14))
    d.prop('bench', (17, 25), (10, 27)); d.prop('cross', (8, 7))
    d.prop('table', (17, 19), (19, 22), (16, 7)); d.prop('chair', (15, 9), (18, 22))
    d.prop('oak', (14, 28), (24, 23), (10, 23)); d.prop('urn', (5, 16), (5, 25))
    d.thing('F', (5, 15), (6, 26)); d.thing('A', (16, 9), (20, 23)); d.thing('H', (23, 20))
    d.thing('g', (17, 9)); d.thing('G', (19, 20)); d.thing('c', (20, 20)); d.thing('q', (4, 25))
    d.haven(9, 8)
    d.monk(9, 9, 'BROTHER MATTHIAS', 'You opened the grain stores for us. The lord called it treason.|The innkeeper hid a key in his room. The abbey stair gate stands at the far eastern end of the north lane. Follow the white candles.')
    d.landmark(12, 20, 12, 'VILLAGE WELL'); d.landmark(18, 7, 5, 'THE EMPTY INN')
    d.landmark(24, 6, 3, 'ABBEY STAIR GATE')
    d.lines += ['@objective Raise the village gate winch and recover three gate keys.',
                '@destination The far east lane gate descends to the abbey cistern.']


def abbey(d):
    m = d.m; m.name = 'THE SILENT ABBEY'
    d.court('Abbey cloister and herb garden', 2, 13, 24, 31, cut=1)
    d.court('West processional way', 2, 3, 8, 17, 'gravel')
    d.court('East processional way', 21, 3, 24, 19, 'gravel')
    d.court('Church foundation and west porch', 7, 3, 23, 16, 'cobble')
    d.building('Church of Saint Alden; nave and sanctuary', 8, 3, 22, 14, (15, 14), wall='1', ceiling=3.25)
    # A second side door links the church to the western return route.
    m.g[7][d.x+8] = '.'; m.door(d.x+8, 7, 'EW')
    d.building('Monastic infirmary', 3, 22, 9, 29, (6, 22), wall='1', ceiling=1.75)
    d.connect('Cistern pump stair opens onto the church porch', (38, 7), (2, 7))
    d.connect('Lower drain returns through the cloister', (34, 26), (2, 26))
    m.route('Garden service gate beyond the southeast cloister', (d.x+24, 30), (m.w-1, 30))
    m.thing('X', (m.w-1, 30)); d.surface('cobble', 24, 30, 26, 30)
    # A six-tread stair brings the -0.5 lower drain up into the garden.
    for i, z in enumerate([-.5, -.375, -.25, -.125, 0]): m.z[26][38+i] = z
    m.z[26][37] = -.5
    d.surface('gravel', 11, 15, 17, 31); d.surface('gravel', 2, 18, 24, 20)
    for x, y in [(10,6),(20,6),(10,10),(20,10)]: m.wall(d.x+x, y, '1')
    d.prop('altar', (15, 5)); d.prop('cross', (15, 4)); d.prop('bench', (12, 9), (18, 9), (12, 11), (18, 11))
    d.prop('candelabra',(13,5),(17,5)); d.prop('statue',(10,4),(20,4))
    d.prop('bench',(4,24),(8,28)); d.prop('urn',(7,24))
    d.prop('well', (20, 24)); d.prop('oak', (4, 17), (22, 29), (10, 28)); d.prop('grave', (23, 17), (23, 19))
    d.key((34,26), (16, 5))
    d.haven(12, 5); d.haven(5, 25)
    d.monk(12, 6, 'BROTHER ANSELM', 'The Jailer was our first gatekeeper. The lord bound his dying oath to five gates.|The sky cannot free you; the whole estate is his prison. Carry each set of keys through its gate to undo that part of the oath.')
    d.monk(5, 26, 'BROTHER LUKAS', 'We hid the final abbey key beside the altar. Take the side porch around the cistern.|Bread and medicine remain in this infirmary. The soldiers abandoned the gardens beyond the next gate, but their hounds did not.')
    d.thing('H', (7, 25), (7, 27)); d.thing('F', (4, 27)); d.thing('A', (18, 16), (23, 26))
    d.thing('S', (18, 19)); d.thing('G', (23, 22)); d.thing('v', (19, 12))
    d.landmark(15, 9, 8, 'CHURCH OF SAINT ALDEN'); d.landmark(17, 24, 10, 'THE HERB CLOISTER'); d.landmark(6, 26, 4, 'INFIRMARY')
    d.landmark(24, 30, 3, 'GARDEN SERVICE GATE')
    d.lines += ['@objective Restore the two drain valves. Ring the bell and recover three keys.',
                '@destination The southeast cloister gate leads to the garden ward.']


def park(d):
    m = d.m; m.name = 'THE GARDEN WARD'
    d.court('Walled park, lawns and orchard', 2, 3, 24, 31, cut=3)
    d.surface('gravel', 3, 14, 24, 16); d.surface('gravel', 11, 4, 13, 30)
    d.surface('earth', 4, 24, 22, 26)
    d.building('Gardeners lodge at the northern fork', 16, 4, 23, 10, (19, 10), wall='3', ceiling=1.75)
    d.connect('Barrack east stair opens onto the park', (40, 15), (2, 15))
    d.connect('South service lane returns to the barrack entrance', (39, 25), (2, 25))
    # A pond can be crossed on its three-cell dry stone bridge or skirted.
    for y in range(17,23):
        for x in range(15,22):
            if y == 20: continue
            d.thing('=' if y != 21 else '>', (x,y))
            m.z[y][d.x+x] = -.125
    d.surface('cobble', 14, 20, 22, 20)
    d.prop('oak', (5,6), (9,9), (5,12), (8,19), (5,22), (7,28), (16,28), (23,28), (21,13))
    d.prop('pine', (14,5), (23,12), (17,12)); d.prop('well', (6,17)); d.prop('cross', (10,5))
    d.prop('bench', (9,15), (13,23), (22,24)); d.key((22,23), (21,6))
    d.prop('oak',(3,17),(3,26),(10,27),(18,30),(24,6),(5,9),(9,22),(22,11))
    d.prop('pine',(3,8),(8,4),(14,30),(24,17),(16,24))
    d.prop('table',(18,6)); d.prop('chair',(20,8))
    d.haven(9, 5)
    d.monk(9,6,'BROTHER PAVEL', 'These paths once fed the village, before the lord locked the grain away.|The gardener carried a key to his lodge. Cross the pond or circle the orchard, then seek the burial gate beyond the northwest barrack ward.')
    d.thing('H',(5,27)); d.thing('F',(18,8)); d.thing('A',(14,15),(22,26)); d.thing('G',(18,14)); d.thing('c',(19,14)); d.thing('s',(18,7))
    d.landmark(10,15,12,'THE WALLED PARK'); d.landmark(19,20,5,'THE STONE BRIDGE'); d.landmark(20,7,4,'GARDENERS LODGE')
    d.lines += ['@objective Defeat the captain, claim his seal and recover three garrison keys.',
                '@destination The northwest barrack gate opens onto the old vault road.']


def graves(d):
    m=d.m; m.name='BLACKWOOD GRAVES'
    d.court('Northern churchyard and burial road',2,3,24,16,'gravel',cut=2)
    d.court('Blackwood forest and southern clearing',2,17,24,31,'leaves',cut=2)
    d.court('Cemetery gate approach',10,14,14,20,'earth')
    d.building('Funerary chapel and abandoned vigil',15,4,23,10,(19,10),wall='1',ceiling=2.5)
    d.connect('Reliquary stair emerges among the graves',(38,13),(2,13))
    d.connect('Receiving cell returns by the old forest road',(37,22),(2,22))
    d.surface('earth',10,18,13,30); d.surface('earth',12,25,23,27)
    d.prop('grave',(4,6),(7,6),(10,6),(4,9),(7,9),(10,9),(5,12),(8,12),(20,13),(23,13))
    d.prop('cross',(12,5),(21,26)); d.prop('altar',(19,6)); d.prop('well',(6,16))
    d.prop('pine',(4,19),(7,21),(4,25),(6,29),(10,29),(15,19),(18,17),(22,18),(16,23),(19,30),(23,23),(23,29))
    d.prop('oak',(9,18),(3,28),(17,29)); d.prop('bench',(17,8))
    d.prop('pine',(3,18),(6,18),(12,18),(21,17),(24,20),(24,24),(24,28),
           (3,23),(3,26),(5,21),(6,23),(8,23),(9,21),(8,27),(8,30),(12,30),
           (15,30),(17,31),(21,30),(22,28),(18,22),(20,20),(21,23),(14,22),
           (15,25),(17,27),(13,28))
    d.prop('oak',(5,30),(19,18),(23,21),(14,20),(5,24),(9,26),(18,24))
    d.prop('pine',(3,4),(13,4),(24,5),(24,9)); d.prop('candelabra',(18,6),(20,6))
    d.key((34,7),(22,26)); d.haven(17,6)
    d.monk(17,7,'BROTHER TOMAS', 'The missing keeper was buried beneath the stone cross in Blackwood. His key was never returned.|The gate to the keep lies beyond the flooded crypt at the far western end. Do not follow a voice off the path.')
    d.thing('A',(12,13),(20,27)); d.thing('H',(21,8)); d.thing('F',(18,8)); d.thing('W',(7,25)); d.thing('S',(20,25)); d.thing('v',(9,7))
    d.landmark(8,10,9,'SAINT ALDENS CHURCHYARD'); d.landmark(19,7,5,'THE VIGIL CHAPEL'); d.landmark(14,25,11,'BLACKWOOD'); d.landmark(21,26,3,'THE KEEPERS GRAVE')
    d.lines += ['@objective Read the three burial inscriptions in order. Recover the gate keys.',
                '@destination The western crypt gate climbs to the warlords keep.']


def last_bell(d):
    m=d.m; m.name='THE LAST BELL'
    d.court('Upper keep court and last bell green',2,3,24,31,cut=3)
    d.surface('cobble',3,14,24,17); d.surface('gravel',11,4,14,30)
    d.building('Bell wardens house',16,4,23,11,(19,11),wall='1',ceiling=2.75)
    d.building('Deserted gate wardens lodge',3,23,10,29,(6,23),wall='3',ceiling=2)
    d.connect('Outer tower path reveals the last open courtyard',(41,15),(2,15))
    d.connect('Armoury supply lane returns from the courtyard',(40,28),(2,28))
    d.prop('well',(8,16)); d.prop('cross',(21,7)); d.prop('oak',(5,7),(8,11),(18,19),(22,25),(15,29),(4,20))
    d.prop('pine',(23,14),(23,29)); d.prop('bench',(14,21),(7,26)); d.prop('altar',(18,7))
    d.key((37,5),(20,7)); d.haven(12,7)
    d.monk(12,8,'BROTHER BENEDIKT', 'The bell warden kept the last key by his altar. The final gate stands above the throne stair.|Each open gate has loosened the oath. Carry the last keys across the bridge. Leave no keeper and no captive behind.')
    d.prop('candelabra',(17,7),(19,7)); d.prop('bench',(17,24),(14,28))
    d.prop('oak',(22,19),(5,12),(8,30),(16,15)); d.prop('statue',(15,5))
    d.thing('O',(18,15)); d.thing('S',(18,9)); d.thing('G',(7,25)); d.thing('A',(17,12),(11,24),(22,9)); d.thing('H',(5,26),(22,20)); d.thing('F',(9,25))
    d.landmark(12,16,12,'THE LAST BELL COURT'); d.landmark(20,8,5,'BELL WARDENS HOUSE')
    # The final approach is an exposed rampart, beyond the occupied nave.
    for y in range(4): d.sky[y][24]=True
    d.lines += ['@objective Release two oath locks, sound the last bell and escape with the keys.',
                '@destination Beyond the last gate: the bridge and the open road.']


def composition(d):
    m=d.m
    if m.number==1:
        d.prop('oak',(3,21),(23,17)); d.prop('bench',(11,22),(16,24))
        d.prop('urn',(15,10),(21,10)); d.prop('table',(16,21)); d.prop('chair',(17,20))
        d.thing('q',(15,21),(16,22))
    elif m.number==2:
        # Stone buttresses break the nave's long exterior walls and frame
        # the entrance; all existing doors and processional routes stay open.
        for x,y in [(7,4),(7,10),(7,13),(23,4),(23,10),(23,13),(10,15),(20,15)]:
            assert m.g[y][d.x+x]=='.' and (x,y) not in d.props
            m.wall(d.x+x,y,'1'); m.c[y][d.x+x]=3.375
        d.prop('urn',(12,22),(16,22),(12,27),(16,27)); d.prop('bench',(14,24),(15,28))
        d.prop('statue',(10,13),(20,13))
    elif m.number==3:
        d.prop('bench',(22,15),(11,23)); d.prop('urn',(17,9),(22,9)); d.prop('statue',(11,11))
    elif m.number==4:
        d.prop('urn',(16,8),(22,8)); d.prop('bench',(11,15))
    else:
        d.prop('candelabra',(14,6),(14,9)); d.prop('statue',(22,28)); d.prop('bench',(7,21))


def production(d):
    """Gameplay authored alongside the district, with checked physical positions."""
    m=d.m
    d.lines.append('@campaign 1')
    def station(x,y,kind,requires,duration,name,hint,local=True):
        wx=d.x+x if local else x
        assert m.g[y][wx]=='.' and (not local or (x,y) not in d.props), (m.number,'mechanism',wx,y)
        d.lines.append(f'@mechanism {wx} {y} {kind} {requires} {duration} {name}|{hint}')
    if m.number==1:
        d.thing('g',(24,10))
        station(24,8,'winch',0,6,'RAISE THE VILLAGE GATE WINCH','THE WINCH IS RELEASED. FIND THE KEYS AND TAKE THE EAST GATE.')
        errand=(7,26,0,'GRAIN TALLY',
                'You fed this village. Find our grain tally in the cooper cottage, southwest of the well. I can spare supplies.|Work the winch beside the far eastern gate, then carry three keys through it.',
                'The tally proves where the grain went.|I have released the hidden guardhouse store. Take medicine, bolts, two decoys and a door brace. Raise the eastern gate winch and bring three keys.')
    elif m.number==2:
        station(8,9,'valve',0,3,'OPEN THE WEST DRAIN VALVE','THE WEST VALVE IS OPEN. RESTORE THE NORTHERN PUMP VALVE.',False)
        station(32,8,'valve',1,3,'OPEN THE NORTHERN PUMP VALVE','THE CISTERN DRAINS. RING THE BELL AT SAINT ALDENS ALTAR.',False)
        station(16,7,'bell',3,0,'RING SAINT ALDENS BELL','THE ABBEY SEAL IS BROKEN. TAKE THREE KEYS TO THE SOUTHEAST GATE.')
        d.lines.append('@drain 16 11 9 10 3')
        errand=(21,12,0,'STOLEN RELIQUARY',
                'Two valves drain the cistern: one in the western antechamber, one in the north pump house. Then ring the altar bell.|Our stolen reliquary lies in the church southeast aisle. Bring it back.',
                'The reliquary belongs where the first keeper swore his oath.|I have released the crypt treasury. Take medicine, bolts, two decoys and a brace. White candles remind him whom he swore to protect.')
    elif m.number==3:
        # The garrison command must be a real encounter, not an invisible flag.
        assert m.g[7][d.x+18]=='s'
        m.g[7][d.x+18]='O'
        station(19,8,'seal',0,0,'CLAIM THE CAPTAINS GARRISON SEAL','THE GARRISON SEAL IS YOURS. TAKE THREE KEYS TO THE NORTHWEST GATE.')
        errand=(22,8,0,'GARDENERS LEDGER',
                'The captain has taken the gardeners lodge. Defeat him, then claim his seal at the desk.|Bring me the ledger from the east side of his room. These gardens once fed the village.',
                'These are the families still waiting outside the walls.|I have opened the concealed officers cache. Take bolts, medicine, two decoys and a brace. Seek the burial gate beyond the northwest barrack ward.')
    elif m.number==4:
        station(4,7,'grave',0,0,'READ THE SOLDIERS BURIAL INSCRIPTION','FIRST: THE SOLDIER. NEXT FIND THE CHILD BENEATH THE EASTERN CROSS.')
        station(10,10,'grave',1,0,'READ THE CHILDS BURIAL INSCRIPTION','SECOND: THE CHILD. LAST FIND THE KEEPER IN SOUTHERN BLACKWOOD.')
        station(21,25,'grave',3,0,'RELEASE THE KEEPERS BURIAL SEAL','THE KEEPER IS REMEMBERED. TAKE THREE KEYS TO THE WESTERN CRYPT GATE.')
        errand=(21,7,0,'MEMORIAL RIBBON',
                'Read the graves in this order: soldier in the northwest, child to the east, keeper beneath the southern forest cross.|A memorial ribbon remains by the chapel east wall. Bring it to me.',
                'Someone still remembers the dead. That is stronger than the lords command.|The receiving treasury is open. Take supplies for the keep. Its gate lies beyond the flooded crypt at the far western end.')
    else:
        station(7,27,'winch',0,2,'RELEASE THE GATE WARDENS OATH LOCK','THE FIRST SEAL BREAKS. FIND ITS TWIN IN THE BELL WARDENS HOUSE.')
        station(21,9,'winch',1,2,'RELEASE THE BELL WARDENS OATH LOCK','BOTH SEALS ARE BROKEN. SOUND THE BELL BESIDE THE ALTAR.')
        station(20,8,'oath',3,0,'SOUND THE LAST BELL','THE OATH IS BROKEN. ESCAPE THROUGH THE GATE ABOVE THE THRONE.')
        errand=(5,25,0,'KEEPERS TESTAMENT',
                'Two oath seals remain: one in the southwest lodge, one in the bell wardens house. Break them, then sound the last bell.|Bring the testament from the southwest lodge before you begin the escape.',
                'He swore to protect people, never to imprison them.|I have released the old chapel treasury. Take supplies, release the oath locks and sound the bell. Run to the gate above the throne when the chains answer.')
    x,y,monk,name,request,thanks=errand
    assert len(request)<224 and len(thanks)<224
    assert m.g[y][d.x+x]=='.' and (x,y) not in d.props, (m.number,'errand',x,y)
    d.lines.append(f'@errand {d.x+x} {y} {monk} {name}~{request}~{thanks}')
    door_x,door_y=[(4,19),(5,31),(15,4),(5,3),(5,3)][m.number-1]
    assert m.g[door_y][door_x]=='Z'
    d.lines.append(f'@erranddoor {door_x} {door_y}')
    # A useful, permanent relic accompanies an existing concealed treasury.
    rx,ry=[(3,18),(3,30),(14,1),(2,2),(3,2)][m.number-1]
    assert m.g[ry][rx]=='T'
    d.lines.append(f'@relic {rx} {ry} {m.number-1}')


def expand(m):
    d = District(m)
    [village, abbey, park, graves, last_bell][m.number-1](d)
    composition(d)
    production(d)
    return d.finish()
