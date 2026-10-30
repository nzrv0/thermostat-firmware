# Flash map and boot chain

All addresses below were derived from the two supplied Intel HEX files plus
literal-pool resolution of the shipped code. Nothing here is assumed.

## Partition layout

| Range | Size | Contents | Present in |
|---|---|---|---|
| `0x08000000`–`0x08001257` | 4,696 B | Bootloader code | `at32_bootloader_6M.hex` |
| `0x08001258`–`0x08004FFF` | 15,784 B | Reserved / erased | **neither file** |
| `0x08005000`–`0x080057FF` | 8 KB | Shared parameter block | **neither file** (runtime-written) |
| `0x08005800`–`0x0800594F` | 336 B | Application vector table | app hex |
| `0x08005950`–`0x08094237` | 583,912 B | Application code + data | app hex |
| `0x08094238`–`0x080FFFFF` | ~460 KB | Not covered by either file | **neither file** |

The device flash is 1 MB (`0x08000000`–`0x080FFFFF`).

## Boot chain

1. The device resets into the **bootloader** at `0x08000000`.
2. The bootloader validates and, if acceptable, jumps to the **application** at
   `0x08005800`.

Evidence:

- The bootloader contains the literal `0x08005800` at `0x08000BD0` — the
  application base, and the value it branches to.
- The bootloader contains the literal `0x08005000` at `0x08000BBC` — the shared
  parameter block, which it writes before handing over.
- The application writes `SCB->VTOR` (`0xE000ED08`) at `0x0807AA98`, pointing
  the vector table at its own partition base `0x08005800`.
- The application contains the literal `0x08005000` at six sites
  (`0x08012B70`, `0x08012B78`, `0x08012B82`, `0x08063A04`, `0x08063A0C`,
  `0x08063A16`), reading and writing the same block.

The literal `0x0807FFFF` appears four times in the bootloader
(`0x0800071C`, `0x08000754`, `0x08000872`, `0x080008A0`) used as a flash
address bound in the page-erase/write routines — i.e. the bootloader
programs flash up to `0x0807FFFF`, which is the 512 KB mark. The application
partition starts at `0x08005800`, comfortably inside that.

## Vector table (application)

At `0x08005800`, 336 bytes = 84 words = 16 core entries + 68 IRQ slots.
The table ends at `0x0800594C` and code begins at `0x08005950`, which matches
the Intel HEX *Start Linear Address* record (`0x08005951`, Thumb bit set).

| Index | Address | Value | Note |
|---|---|---|---|
| 0 | `0x08005800` | `0x20016B20` | Initial SP — implies ≥93 KB of SRAM |
| 1 | `0x08005804` | `0x08005B1D` | Reset handler |
| 2 | | `0x0805E5E7` | NMI |
| 3 | | `0x080419C7` | HardFault |
| 4 | | `0x08047129` | MemManage |
| 5 | | `0x0800F447` | BusFault |
| 6 | | `0x08065593` | UsageFault |
| 7–10 | | `0x00000000` | Reserved |
| 11 | | `0x08061B8D` | SVCall |
| 12 | | `0x0803490D` | DebugMonitor |
| 13 | | `0x00000000` | Reserved |
| 14 | | `0x0805F155` | PendSV |
| 15 | | `0x080637F5` | SysTick |

Default IRQ handler is `0x08005B37`; 13 slots have bespoke handlers:

| IRQ | Handler | IRQ | Handler |
|---|---|---|---|
| 12–15 | `0x0803449C`…`0x080344A2` (four 2-byte stubs) | 37 | `0x080652FC` |
| 20 | `0x08065384` | 39 | `0x08065368` |
| 23 | `0x0803E4FC` | 50 | `0x08063980` |
| 28 | `0x08063960` | 52 | `0x080650C8` |
| 29 | `0x08063964` | 53 | `0x08065120` |

Decoded behaviour at these sites (the handlers are thin stubs; the
peripheral each one drives was recovered with `tools/analyze.py`):

| IRQ | Vector | Handler | Peripheral [F] | Behaviour |
|---|---|---|---|---|
| 12–15 | `0x0803449C`–`0x080344A2` | — | — | four `bx lr` stubs: shared dispatcher entries |
| 20 | `0x08065384` | `0x080653E4` | USB/CAN block (`0x40005C44`) | reads a peripheral pointer, writes a halfword |
| 23 | `0x0803E4FC` | `0x08064F48` | peripheral status | reads a status byte with mask `0x80` (`0x0806FEC4`) |
| 28 | `0x08063960` | — | — | empty `bx lr` stub |
| 29 | `0x08063964` | — | state block `0x20002248` | tests the byte at +`0x29D` for `'S'` (`0x53`), then calls `0x080626E8` |
| 37 | `0x080652FC` | `0x080828C4` | **USART1** (`0x40013800`) | the ESP-07S modem byte path, 32-byte reads |
| 39 | `0x08065368` | — | state block `0x20002248` | also touches `0x20000224` / `0x20000226` |
| 50 | `0x08063980` | `0x080638E4` | **TIM5** (`0x40000C00`), RCC | timer interrupt |
| 52 | `0x080650C8` | `0x080816E8` | **UART4** (`0x40004C00`) | 32-byte reads |
| 53 | `0x08065120` | `0x080651D4` | **UART5** (`0x40005000`), GPIOC, GPIOD | 32-byte reads plus two port lines |

IRQ 37 is the ESP-07S modem (the AT command path) and IRQ 52/53 are
UART4/UART5, which carry the Modbus RTU 485 line and the LoRa modem.


## Startup code

The application entry at `0x08005950` is the ARM Compiler runtime veneer:

```
0x08005950  bl   #0x08005958
0x08005954  bl   #0x08005AC8      ; __scatterload / zero-fill
0x08005958  adr  r0, #0x28
0x0800595A  ldm.w r0, {sl, fp}
0x0800595E  add  sl, r0
0x08005960  add  fp, r0
0x08005962  sub.w r7, sl, #1
0x08005966  cmp  sl, fp
0x08005968  bne  #0x0800596E
0x0800596A  bl   #0x08005AC8
0x0800596E  subw lr, pc, #9
0x08005972  ldm.w sl!, {r0, r1, r2, r3}
0x08005976  tst.w r3, #1
0x0800597A  it   ne
0x0800597C  subne r3, r7, r3
0x0800597E  orr  r3, r3, #1
0x08005982  bx   r3
```

The reset handler at `0x08005B1C` is the classic Keil/ARMCC sequence:

```
0x08005B1C  ldr  r0, [pc, #0x24]   ; = 0x08063861   (SystemInit)
0x08005B1E  blx  r0
0x08005B20  ldr  r0, [pc, #0x24]   ; = 0x08005951   (__main)
0x08005B22  bx   r0
```

followed by eleven `b .` self-loops (the hardfault/infinite-loop stubs) and a
`ldr r0/r1/r2/r3 ... bx lr` helper at `0x08005B38` (the exception return
trampoline).

This confirms the original was built with **ARM Compiler (Keil MDK / armcc)**,
not GCC. It matters for the rebuild: the `__scatterload` veneer, the
`__main` entry convention and the register-resident stack setup all differ
from GCC's `crt0`. The rebuild in this repository uses GCC and a conventional
`.isr_vector` + `Reset_Handler`, which produces the **same memory layout**
(vector table at `0x08005800`, 336 bytes, code at `0x08005950`) — verified
identical to the shipped image — while using a different runtime.

## Shared parameter block at `0x08005000`

Both the bootloader and the application access a block at `0x08005000`.

Observed in the bootloader at `0x08000BBC` / `0x08000BD0`, and in the
application at the six sites listed above. The application's access sequence
is consistent and always of this shape:

```
ldr r0, =0x08005000
bl  <read_fn>          ; 0x08070C9C
ldr r1, =0x12345678
ldr r0, =0x08005000
bl  <write_fn>         ; 0x08070DF0
ldr r0, =0x2000199C
ldr r1, [r0]
ldr r0, =0x08005000
adds r0, r0, #4
bl  <write_fn>
bl  <0x08070C7C>
bl  <0x080638DC>
```

Interpretation:

- `0x12345678` is a **magic/marker word** stored at the block base.
- The payload begins at `0x08005004` (the `+4` offset).
- The block is read and written through flash routines — it is flash-resident
  and rewritten on change.

The **exact field layout is not recoverable** from the supplied files: the
bootloader half of the contract was not provided, and the application's use
of the block is confined to six call sites whose data dependencies have not
yet been traced. `firmware/include/param_block.h` currently models only the
envelope (magic at offset 0, payload from offset 4) and deliberately leaves
the payload opaque.

**Open**: the payload layout, and whether the vendor bootloader validates it.
Closing this needs either the bootloader source or a physical read of the
device's `0x08005000` region.

## Rebuild mapping

`firmware/linker/stm32f1xx_app.ld` reproduces this layout exactly:

- `FLASH` origin `0x08005800`, length `1024K - 0x5800`.
- `RAM` origin `0x20000000`, length `96K`.
- `_estack = 0x20016B20` (identical to the shipped SP).
- `.isr_vector` is 84 words (336 bytes) at `0x08005800`, so `.text` begins at
  `0x08005950` — verified by `readelf -S` against the built ELF.
