# Vision communication

## Stable v1 contract

The public C interface is `1.0` and the wire version is `1`, exposed by
`DEVICE_VISION_INTERFACE_VERSION_*` and `DEVICE_VISION_WIRE_VERSION`.
Published enum values are explicit and must not be renumbered. Existing
function signatures, structure-field meanings, frame names, payload order and
units are compatibility contracts. An additive API may increment the interface
minor version; changing an existing contract requires a new major/wire version.

Every frame uses:

```text
@1|TYPE|SEQ|LEN|PAYLOAD|CRC16\n
```

The stable v1 payloads are:

| Direction | Type | Payload |
|---|---|---|
| either | `ACK` | `confirmed_session_hex8` |
| MCU -> camera | `START`, `END` | `session_hex8` |
| MCU -> camera | `REQUEST` | `session_hex8,COLOR|SHAPE|BOTH` |
| MCU -> camera | `CONTEXT` | `session_hex8,context` or `session_hex8,context,value2` |
| MCU -> camera | route `CONTEXT` | `session_hex8,mode,from,to,heading` |
| camera -> MCU | `TRACK` | `session_hex8,from,to,lane_x,lane_angle_ddeg,junction_y,confidence_pm` |
| camera -> MCU | `JUNCTION` | `session_hex8,from,to,cx,cy,left_ddeg,front_ddeg,right_ddeg,confidence_pm` |
| camera -> MCU | `DETECTION` | `session_hex8,from,to,slot,letter,class,shape,confidence_pm,cx,cy` |
| camera -> MCU | `SCAN_DONE` | `session_hex8,from,to,letter_mask` |
| either | `RESULT` | `session_hex8,NONE,color,shape` or `session_hex8,request_session_hex8,request_seq_hex4,color,shape` |

Sessions are nonzero boot epochs. Point values are `0..7`, route headings are
`0..5`, direction slots are `0..2`, confidence is per mille `0..1000`, angles
are deci-degrees `0..3599`, detection letters are `A..N`, and letter masks use
the low fourteen bits. These are transport-level ranges only; their map and
product meanings remain owned by the caller.

`Device_Vision` implements the CRC16 packet protocol and one reliable stream
in each direction. MCU critical commands are START, CONTEXT and END. Camera
critical events are JUNCTION, DETECTION and SCAN_DONE. ACK payloads name the
session being acknowledged; duplicates are re-ACKed without duplicate state.

TRACK is latest-value telemetry and is not queued. It carries the active red
field point pair, lane center, lane angle, visible Y row and confidence.

The Product Task assigns the application meaning of point codes, scan slots and
letter masks. The Device layer only validates field ranges, CRC, sessions and
sequence/retry semantics.
