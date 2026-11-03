# Level format

A level is a KA3D `.hgr` scene (format 1.93, exporter 0x020904, platform id 5) exported from 3ds Max. Gameplay is
not code: it lives in the **user properties** of the nodes, one `key=value` per line. `tools/hgr-dump` prints them.
Levels are in `data/bundles/original/`; reusable objects (ball, apples) in `data/resources/models/`.

## Node naming

| Prefix | Meaning | Examples |
| --- | --- | --- |
| `T_` | Trigger (action list) | `T_link_ab`, `T_attraction_start`, `T_boost` |
| `O_` | Object or camera driven by the logic | `O_startcamera`, `O_logpuzzle` |
| `N_` | Level section linked and unlinked as the ball moves | `N_a` ... `N_g` |
| `S_` | Ambient sound emitter | `S_rapid` |
| `D_` | Marker (start, checkpoints, rail starts) | `D_start`, `D_checkpoint_08` |
| `L_` | Rail (`Lines` nodes `track` and `tracknormal`) | `L_hillsrail01` |
| `*_COL` | Collision mesh | `ground_b_COL` |

## Collision meshes and volumes

| Key | Meaning |
| --- | --- |
| `collisionmesh=this` | the node is collision geometry |
| `group=N` | bit mask; bit `0x100` = collision only (never drawn), bit `0x400` = sensor (passable, reports contact) |
| `executetrigger=T_x` | touching the mesh activates the trigger |
| `collisiontype=OBB` / `SPHERE`, `radius=N` | trigger volume; activated when the ball enters it |

## Library objects

A marker with `load=file,node` clones `resources/models/<file>.hgr`, takes `node`, replaces its transform with the
marker's and renames it to the marker's name; the marker becomes `T_<marker>`. With `shadow=0` the model's own
shadow is hidden. This is how apples (`billero`) are placed: `item=collectable`, `collisiontype=SPHERE`, `radius`
and their actions (`addscore`, `particlefx`, `playsound`, `unlink,this`).

## Triggers

| Key | Meaning |
| --- | --- |
| `a01`, `a02`... | main action list, in order until a number is missing (`a100`+ uses three digits) |
| `e01`, `e02`... | exit list |
| `state` | initial state: 0 disabled, 1 enabled (waits for activation, default), 2 running |
| `executetype` | after the exit list: 0 leaves the trigger disabled, otherwise it is enabled again (default 1) |

Every frame a running trigger executes actions until one asks to wait (`delay`, an unmet condition, a jump). A
trigger disabled while running finishes its list and stays disabled. `activatetrigger` runs the target immediately
(until its first wait) before the caller continues; a trigger that is already running is not restarted.

Conditions `less|equal|greater|notequal,a,b[,target]` compare numbers or variables: when true execution continues,
otherwise it jumps to action `target` (1 based); without a target the condition is re-evaluated every frame.

## Actions

The name table (88 entries) comes from the original binary (`ResourceManager::ResourceManager`), indexed like the
`switch` in `Action::parseProperties`. *Interpreter* actions live in `game/src/Actions.cpp`; *world* actions are
handled by `port/game/SceneWorld.cpp` and `BallController`. *Parsed* actions are recognized and ignored.

| Action | Arguments | Effect | Port |
| --- | --- | --- | --- |
| `nop` | | jump target | interpreter |
| `delay`, `randomdelay` | seconds / min, max | wait | interpreter |
| `jumptoaction`, `jumptorandomaction` | N / N1, N2... | jump | interpreter |
| `set`, `setpersistent`, `add`, `sub`, `mul`, `div` | var, value | variables | interpreter |
| `less`, `equal`, `greater`, `notequal` | a, b[, target] | conditions | interpreter |
| `enabletrigger`, `disabletrigger`, `activatetrigger`, `activaterandomtrigger` | T1, T2... | trigger state | interpreter |
| `link`, `unlink` | parent, nodes... / nodes... | scene graph (parent `scene` = root) | interpreter |
| `animate` | node[, wait] | whole node animation | interpreter |
| `playanimation` | node, anim, target, first frame, last frame... | frame range at 30 fps | interpreter |
| `moveobject`, `rotateobject`, `rotateto`, `scaleobject` | object, subnode, x, y, z, seconds[, wait] | transform over time | interpreter |
| `moveuv` | object, subnode, u, v, seconds | texture scrolling (water, waterfalls) | interpreter |
| `fade` | seconds, direction, color | 1 fades in from the color, 0 fades out to it; waits | interpreter |
| `waitforkeypress` | key (`lmouse`) | wait for a tap | interpreter |
| `timer` | var | counts the variable down, continues at zero | interpreter |
| `addscore` | n | score | world |
| `particlefx` | effect.prs, node... | particles | world |
| `playsound`, `stopsound` | file | audio | world |
| `pressure` | strength | vibration | world |
| `checkpoint`, `levelfailed` | node | respawn point / return to it | world |
| `levelcomplete`, `levelend` | | end of level, results | world |
| `setcamera` | camera (`camera` = gameplay camera) | active camera | world |
| `setcameraview` | height, distance | rail camera | world |
| `setgamemode` | mode | game mode | world |
| `attachtorail` | ball, rail, direction | rail mode (level 2) | world |
| `speedmul`, `maxspeed` | ball, value | ball speed | world |
| `setobjectpos` | object, marker | teleport (respawn on rails) | world |
| `updateballvalues`, `resetballvalues` | | ball parameters from variables / from `D_forminfo` | world |
| `setballdirection`, `setcamprops` | | | parsed |
| others (`addimpulse`, `changeball`, `setgravity`, `showtext`, minigames...) | | unused by these levels | parsed |
