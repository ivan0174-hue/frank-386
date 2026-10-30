# FRANK 386 per Raspberry Pi 4 — versione bare metal

Port dell'emulatore PC i386 **FRANK 386** (originariamente per RP2350 / Raspberry Pi Pico 2)
sul **Raspberry Pi 4**, eseguito **bare metal**: niente Linux, il kernel dell'emulatore
parte direttamente dalla SD dopo il firmware del Pi, in circa 2 secondi.

Il core dell'emulatore (CPU i386/486 con FPU x87, VGA/VBE, IDE, floppy, Sound Blaster 16,
AdLib, MPU-401, PC speaker, Covox, Tandy, Disney Sound Source, disk manager e menu
impostazioni) è lo stesso del firmware RP2350: cambia solo lo strato hardware.

## Due modi di usarlo

| Modalità | Dove gira | A cosa serve |
|---|---|---|
| **Bare metal** (`port/circle`) | Raspberry Pi 4 / 400 / CM4, direttamente sull'hardware | Uso normale: avvio istantaneo, tutto il Pi dedicato all'emulatore |
| **Linux/SDL2** (`port/sdl`) | Raspberry Pi OS 64 bit, oppure un PC Linux | Provare senza riscrivere la SD, sviluppo e test |

Entrambe usano lo stesso layout della scheda SD (cartella `386/`), quindi le immagini disco
e il `config.ini` sono intercambiabili.

## Preparare la scheda SD (bare metal)

1. Formatta una micro-SD con **una sola partizione FAT32** (tabella MBR).
2. Copia nella radice della partizione tutto il contenuto di `dist/sdcard/`
   (oppure della cartella `sdcard/` dello zip già compilato):

   ```
   start4.elf  fixup4.dat  bcm2711-rpi-4-b.dtb  bcm2711-rpi-400.dtb  bcm2711-rpi-cm4.dtb
   armstub8-rpi4.bin  kernel8-rpi4.img  config.txt  cmdline.txt
   386/bios.bin  386/vgabios.bin  386/fdd0.img  386/config.ini
   ```
3. Metti le tue immagini disco in `386/` e indicale in `386/config.ini`.
4. Collega monitor HDMI (porta HDMI0, quella vicina all'alimentazione), tastiera USB
   e, se vuoi, mouse e gamepad USB. Accendi.

La SD fornita avvia DR-DOS 8.1 dal floppy `fdd0.img`, incluso nel progetto originale.

### `386/config.ini`

```ini
[pc]
mem=16M          ; RAM del PC emulato (1M..256M). Windows 95: 32M o più
vga_mem=4M       ; VRAM (256K..16M); 4M abilita i modi VBE ad alta risoluzione
bios=bios.bin
vga_bios=vgabios.bin
fda=fdd0.img     ; floppy A:   (fdb= per B:)
;hda=dos622.img  ; disco C:    (hdb=, hdc=, hdd=)
;cdc=cdrom.iso   ; CD-ROM      (cda= .. cdd=)

[cpu]
gen=4            ; 3 = 386, 4 = 486, 5 = Pentium (parziale)
fpu=1

[frank-386]
pcspeaker=1
adlib=1
soundblaster=1
mpu401=1
mouse=1
usb_joystick=1
volume=15
```

Le impostazioni modificate dal menu (Win+F11) vengono salvate in
`/.config/386/RPI4/config.ini`, che ha la precedenza su `386/config.ini`.

### `cmdline.txt` (una sola riga)

| Opzione | Effetto |
|---|---|
| `width=1280 height=720` | dimensione del framebuffer; la GPU la scala alla risoluzione del monitor |
| `sounddev=sndpwm` | audio solo dal jack da 3,5 mm |
| `sounddev=sndhdmi` | audio solo da HDMI |
| *(nessun `sounddev`)* | HDMI, con ripiego automatico sul jack se il monitor non ha audio |
| `loglevel=4` | dettaglio del log seriale (0–5) |

Il log di avvio e i messaggi dell'emulatore escono sulla **seriale** (GPIO14 = TX,
115200 8N1): utile se qualcosa non parte.

### `config.txt`

È quello di Circle per AArch64 con in più `disable_overscan=1`, `hdmi_force_hotplug=1`
e `hdmi_drive=2` (audio HDMI). Contiene, commentato, un overclock a 2 GHz: l'emulatore è
limitato dalla CPU, quindi con un buon dissipatore conviene abilitarlo.

## Comandi

| Tasti | Funzione |
|---|---|
| **Win + F12** | Disk manager: cambia floppy, dischi e CD a caldo |
| **Win + F11** | Menu impostazioni (CPU, FPU, schede audio, mouse, volume) |
| tastiera USB | Arriva al PC emulato come tastiera PS/2 (set 1); il tasto `<>` delle tastiere italiane funziona |
| mouse USB | Mouse PS/2 (carica `CTMOUSE` in DOS) |
| gamepad USB | Joystick analogico sulla porta giochi 0x201 (con `usb_joystick=1`) |

## Compilare

### Bare metal

Da un PC Linux x86-64 o da un Raspberry Pi con OS a 64 bit:

```sh
sudo apt install build-essential git curl xz-utils texinfo
cd port/circle
./build.sh
```

Lo script scarica (una volta sola) la toolchain Arm GNU `aarch64-none-elf` 13.3 e
**circle-stdlib** (Circle Step 51.1.1 + newlib), li compila per Pi 4 in modalità AArch64
multi-core, compila `kernel8-rpi4.img` e prepara `dist/sdcard/` con firmware e file di
configurazione. Per ricompilare solo il kernel: `make CIRCLE_STDLIB=/percorso/circle-stdlib`.

### Linux / SDL2

```sh
sudo apt install build-essential libsdl2-dev mtools dosfstools
cd port/sdl
make
./make-sd-image.sh        # crea sdcard.img (FAT32) con la cartella 386/ di esempio
./frank386 sdcard.img
```

Clic nella finestra per catturare il mouse, **Ctrl+F10** per rilasciarlo,
**Alt+Invio** per lo schermo intero.

## Come funziona il port

```
                ┌──────────────── Raspberry Pi 4 (BCM2711, 4× Cortex-A72) ────────────────┐
  SD (FAT32) ── │ core 0: CPU i386 + chipset PC, FatFs su EMMC2, IRQ USB (xHCI)           │
  USB HID   ──▶ │         renderer VGA software → frame XRGB ──┐                           │
                │ core 1: mixer audio + sintesi OPL ──▶ coda HDMI / PWM ──▶ audio         │
                │ core 2: scala il frame (4:3, triplo buffer) ──▶ framebuffer HDMI         │
                │ core 3: libero                                                         │
                └─────────────────────────────────────────────────────────────────────────┘
```

- **`port/compat/`** — sostituti minimi degli header del Pico SDK (`pico.h`, `pico/time.h`,
  `hardware/*.h`): le macro di sezione RP2350 spariscono, il tempo e i *repeating timer*
  passano dall'HAL. Così il codice dell'emulatore resta quasi identico all'originale.
- **`port/common/`** — front-end comune alle due versioni: `frank_main.c` (ex `src/main.c`
  senza PIO/PSRAM/clock), mixer audio a blocchi, OSD software per menu e messaggi,
  `diskio` di FatFs sopra l'HAL, tabella tasti USB HID → codici Linux.
- **`port/circle/`** — backend bare metal basato su [Circle](https://github.com/rsta2/circle):
  framebuffer HDMI con page flipping, USB (tastiera, mouse, gamepad), EMMC2, audio HDMI/PWM,
  multicore.
- **`port/sdl/`** — backend SDL2 per Linux.

Modifiche al codice originale in `src/` (tutte dietro `#ifdef FRANK_PORT`, la build RP2350
non cambia):

- `pc.c`: RAM e VRAM allocate dall'heap invece che agli indirizzi fissi della PSRAM;
  `mem=` e `vga_mem=` di nuovo configurabili.
- `vga.c`: usa il renderer software generico di Tiny386 (già presente) invece dello
  scanline renderer PIO.
- `i8042.c`, `misc.c`: niente pthread/termios; timer CMOS dall'HAL.
- `settingsui.c`: nasconde le voci solo-RP2350 (clock, tensione, PSRAM, uscita VGA/I2S).
- `board_config.h`: definizioni della scheda Pi 4 in `port/compat/port_board_config.h`.

## Stato e limiti

- Verificato: la build Linux/SDL avvia SeaBIOS e DR-DOS, esegue programmi (MEM, Norton
  SysInfo), il modo grafico 13h e i menu OSD; il kernel bare metal, compilato con lo stesso
  codice per Pi 3 ed eseguito in QEMU, monta la SD, carica il BIOS e avvia DR-DOS
  mostrando l'immagine sull'uscita HDMI emulata.
- **Non ancora provato su un Raspberry Pi 4 reale**: USB xHCI, EMMC2 e audio HDMI/PWM usano
  i driver standard di Circle, ma vanno verificati sull'hardware. Se qualcosa non va,
  il log sulla seriale (GPIO14) dice in che punto.
- L'emulatore è un interprete: un Pi 4 a 1,5 GHz è molto più veloce di un RP2350, ma
  non aspettarti un Pentium; l'overclock a 2 GHz aiuta.
- Rete (NE2000) disattivata come nella versione RP2350.

## Licenze

- FRANK 386 e Tiny386: MIT (vedi `LICENSE`).
- Circle è **GPLv3**: il kernel bare metal `kernel8-rpi4.img`, che lo include, è quindi
  distribuito sotto GPLv3. La build Linux/SDL non usa Circle.
- Firmware Raspberry Pi (`start4.elf`, `fixup4.dat`, `*.dtb`): licenze Broadcom/Linux
  incluse nella SD (`LICENCE.broadcom`, `COPYING.linux`).
