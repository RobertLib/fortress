# Campaign geometry and routes

The campaign now crosses an estate as well as its interior floors. See
[the complete story and world design](story-and-world.md) for the narrative,
chapter progression, combat pacing and implementation boundaries. Outdoor
districts are authored in `tools/author_world.py` and exported together with
the interior plans by `tools/author_campaign.py`.

| Chapter | Expanded size | Required district | Two connections |
| --- | --- | --- | --- |
| The Outer Ward | 66 × 33 | Village green, well, cottages and inn key | Market arch and north lane stair |
| The Silent Abbey | 68 × 35 | Church, altar key, cloister and infirmary | Pump stair and lower drain ascent |
| The Garden Ward | 70 × 35 | Park, pond bridge and gardener's lodge key | East stair and south service lane |
| Blackwood Graves | 68 × 35 | Churchyard, vigil chapel and forest grave key | Reliquary stair and receiving-cell passage |
| The Last Bell | 72 × 37 | Upper court and bell warden's house key | Tower path and armoury supply lane |

Each district has a required key, a well, monks, additional supplies and
landmarks. Local ground materials include grass, earth, cobbles, gravel and
leaf litter. Roofs occlude a directional night sky. Tall facades, door headers
and pitched-roof gables continue the wall's authored material at the same
texture scale, orientation and lighting. Windows and banners painted into wall
tiles stay on the ground storey, with bare masonry above them. The dimensions below
describe the retained interior plans, before the district extension. Roofs
project beyond the walls with solid timber undersides and edge boards; larger
timber houses have masonry chimneys. The inn has a steeper roof with its ridge
parallel to the entrance facade and shuttered windows on both storeys.
Window faces and the adjoining space occupied by wide church windows are
reserved before torches are placed, keeping flames and brackets off the frames.

Each exit stands on the far side of its chapter from the player start:
the eastern end of the village north lane, the southeast cloister, beyond
the northwest barrack sleeping ward, beyond the western flooded crypt,
and above the keep's throne stair. The entry rooms have no exit gates.
All gates require three keys and the chapter's mechanisms; the district
connections remain loops. [The production upgrade](production-upgrade.md)
documents their map directives, state and verification.

All five interior floor plans are explicitly authored. They retain the three-key horror maze
loop, but replace the old tile arrangements and global height bands with local
stairs, galleries, lowered chambers and carefully framed connections.

The plans are explicitly specified in `tools/author_campaign.py`; there is no
random layout generation. Exporting validates each door's two jambs, both
approaches, floor elevations, ceiling clearance, object occupancy and story
positions before writing the runtime text maps.

## The five plans

| Floor | Size | Distinct geometry | Exploration and return |
|---|---|---|---|
| Guardhouse | 39 × 33 | Raised watch chapel; bevelled nave with a lowered pool; upper gallery seen through bars | Three key branches reconnect through the north passage and eastern stair. A barred cell visible at the start is entered later on a separate return circuit. |
| Cellars | 41 × 35 | Deep cistern split by offset masonry islands and a raised crossing; lower flooded crypt; upper pump house | A northern inspection lodge and lower drain circuit offer different routes between keys. Opposed cistern stairs and the outer pump stair reconnect the elevation levels. |
| Barracks | 43 × 35 | Exercise hall under a barred gallery; raised officers' quarters; low bathing room and private chapel | The upper gallery has stairs at both ends. Western sleeping and armoury passages interleave with the southern bath route and the eastern service circuit. |
| Vault | 41 × 35 | Octagonal circuit around a solid tomb; deeper burial stack; raised reliquary previewed through a low window | The northern spiral and lower crypt route meet around the tomb. The reliquary window shows an objective before the player finds either of its approaches. |
| Warlord's Keep | 45 × 37 | Raised throne nave with low aisles and piers; higher apse; sunken cloister; upper tower | Western chapel, eastern armoury and tower branches form separate searches. Stair returns, breaches and the outer corridor lead back to the final ascent behind the throne. |

The largest chambers contain structural partitions, water, piers or a central
obstruction. Winding one-cell passages and small intermediate rooms preserve
blind corners, the sound of an unseen pursuer, and meaningful doorways. Each
floor has two concealed caches and two breakable passages that shorten an
otherwise longer route. Neither type is required to find keys or use the gate.

Floor elevations are local to the architecture: they do not sweep across the
entire map in parallel strips. Ceilings are independent and form low passages,
chamber vaults, permanent door lintels and stepped upper spaces. The engine
still uses one floor and ceiling per square cell; there are no stacked rooms,
lifts or jumping.

## Horror and physical clearance

The sparse torch budget remains. Story fragments, white-candle refuges and
hauntings are repositioned in the new spaces, with long unoccupied linking
passages between encounters. Keys still trigger their omens and permit Jailer
encounters. Loops let the player break sight, close distance to a refuge or
return through a familiar room from a different side.

A door must occupy a real one-cell aperture in a continuous wall. It must have
two solid jambs, two usable approaches and compatible floor heights. The source
exporter and runtime regression both reject freestanding or dead-end doors.

Projecting fireplaces and candelabra in the new maps need a two-deep,
three-wide furnishing bay. A T-junction cannot masquerade as a suitable room
and place an obstacle in the centre of a narrow crossing. Existing custom maps
without independent ceilings retain their previous furnishing layout.

## References

The research informed spatial contrast, recognizable architecture, glimpses of
later destinations, returning routes and concealed rewards:

- [Heretic E1M1: The Docks](https://strategywiki.org/wiki/Heretic/E1M1%3A_The_Docks)
- [Heretic E1M2: The Dungeons](https://www.gamers.org/games/heretic/secrets/sec12.html)
- [Heretic E1M6: The Cathedral](https://strategywiki.org/wiki/Heretic/E1M6%3A_The_Cathedral)
- [Hexen: Seven Portals and Guardian of Ice](https://www.heretichexen.com/hexen/walkthrough/)

These are design interpretations rather than copied maps. Fortress retains its
own three-key progression, horror pacing and original artwork.

## Verification

Run `python3 tools/author_campaign.py` to export all five maps, or pass one or
more floor numbers. Direct edits to `levels/N.txt` are possible, but exporting
again replaces them from the source plans.

Run `ctest --test-dir build --output-on-failure`. The architecture regression:

- Rejects example floating and dead-end doors and checks every campaign door.
- Requires substantial enclosed passages alongside the new open districts.
- Requires exactly one exit, separated from the start by at least half the
  map's width or height.
- Completes all six key orders on every floor, walks to and operates each
  required mechanism in prerequisite order, then walks to and uses the
  gate with real movement, use input, door timing, stair traversal and pickups.
- Keeps barrels and furniture solid; secret walls and breaches stay closed,
  and mirrors are unused during those thirty full routes.
- Checks each breach has a meaningful detour while closed and becomes a
  physically traversable two-crossing connection after destruction.
- Checks reward, note and refuge access, ceiling occlusion and save signatures.

Combat and trap damage are isolated from the traversal runs; their own suites
exercise attacks, the Jailer's behaviour, traps and combat. Omens, notes,
haunted-door behaviour, water drag and ordinary door closure still run during
the controlled walks. These checks support visual and play review; test counts
do not establish subjective level quality.

`./build/architecture_tests --screenshots` exports starting and landmark views,
complete plans and every door both closed and open. `./build/stair_tests
--screenshots` adds one view of an authored stair flight per floor. Images are
rendered by the actual game engine in `build/architecture-floor-N-V.bmp`,
`build/architecture-map-N.bmp`, `build/maze-door-N-DD-S.bmp` and
`build/stairs-floor-N.bmp`.

`./build/world_tests --screenshots` adds outdoor, building, monk and dialogue
views in `build/world-N-*.bmp`. Its separate regressions cover roof/sky
occlusion, solid wall and roof tops, scenery beyond low walls, door headers,
facade material and UV continuity across both gable axes and roof coverings,
terrain, peaceful dialogue, map validation and save compatibility. The geometry
previews are `build/world-wall-cap.bmp`, `build/world-door-header.bmp` and
`build/world-inn-facade.bmp`; both sides of the inn's doors are checked closed,
partly open and fully open.
`build/world-inn-house-0.bmp` through `world-inn-house-2.bmp` show the complete
inn from the square and both side approaches, using walkable player positions.
Flat-material previews in `build/world-distant-lighting-N.bmp` check that
oblique facades and headers keep continuous lighting at a distance, including
the roof edge where a second coplanar surface would cause striping.
