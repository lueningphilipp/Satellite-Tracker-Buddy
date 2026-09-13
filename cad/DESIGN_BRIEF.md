# Frame/Enclosure Design Brief — Satellite Ground-Track Display

This is a brief for an LLM (any LLM, with or without image generation) to
produce **concept ideas** for a 3D-printable desk enclosure — not CAD files
themselves. The output should be a handful of distinct visual/structural
directions (descriptions, and rendered concept images if you're able to
generate them) that a human will then model for real in Fusion 360 /
OnShape / Blender / etc. and 3D print (FDM). Read this whole brief before
proposing anything — the functional constraints in particular aren't
optional.

## What this thing is

A desk gadget that tracks a satellite's live position and shows it on a
small e-paper display: a world map with the satellite's ground track, plus
some text (name, apogee/perigee, orbit class, etc.). It's WiFi-connected,
always on, sits on a desk or shelf, and needs a physical frame/enclosure to
hold the electronics and present the screen nicely. Think "small e-ink
digital photo frame," not "phone" — it's meant to be glanced at, not
touched much, and it never needs a battery swap (it's USB-powered,
permanently plugged in).

## What has to physically fit inside

Two PCBs, connected by a ribbon (FPC) cable, both need to fit inside the
enclosure:

1. **The e-paper display module**: a Waveshare 7.5" V2 e-paper panel.
   - Outline (whole module, including its glass/panel and small carrier
     PCB): **approximately 194mm × 137mm**, a few mm thick at the panel
     itself, with a slightly raised/thicker strip along one edge where the
     FPC ribbon connector sits.
   - **Active/visible image area** (what needs an unobstructed cutout for
     viewing): approximately **163mm × 98mm**, centered within the outline
     above but offset toward one edge (check the real datasheet — it's not
     perfectly centered).
   - **These numbers are approximate — before finalizing any cutout or
     mounting-hole positions, pull the exact outline/mounting drawing from
     Waveshare's own wiki page for this exact panel (search "Waveshare
     7.5inch e-Paper HAT V2 wiki") — it publishes a dimensioned drawing
     with mounting hole positions.**
   - The panel connects via a short, fairly stiff orange FPC ribbon cable
     to the driver board (see below) — this cable has a minimum bend
     radius, don't plan a design that forces a tight fold in it.

2. **The driver board**: Waveshare's "e-Paper ESP32 Driver Board" — a
   small PCB (an ESP32-WROOM dev board layout with an FPC socket for the
   panel and a USB port), roughly **100mm × 65mm** (approximate — verify
   against Waveshare's product page for this exact board). It has:
   - A **USB port** (power + programming) that needs to be reachable from
     outside the enclosure without disassembly — someone should be able to
     plug in a cable to power it permanently, and occasionally re-flash it
     without opening the case.
   - A physical **BOOT button** on the board itself, which this project
     uses for a real function (hold 3s to reset WiFi) — **it needs to be
     physically press-able from outside the enclosure**, either through a
     cutout/hole positioned over it, or via a small pass-through
     button/plunger if you want a cleaner front face. This is a real,
     currently-used feature, not optional to omit.
   - No display, no other user controls on this board.

## Functional requirements (non-negotiable)

- **Screen cutout**: unobstructed view of the full active display area
  (~163×98mm), square edges, no bezel encroaching on the image.
- **Viewing angle**: this sits on a desk and is looked at from a normal
  seated position a meter or so away — propped up at an angle (like a
  photo frame or a small easel/kickstand), not lying flat. A viewing angle
  in the ballpark of 60-75° from horizontal (fairly upright, slight
  backward lean) is typical for this kind of desk object — feel free to
  explore variations, but "flat on the desk" doesn't work.
- **USB cable exit**: a clean way for a USB cable to exit the back or
  bottom and reach the driver board's port without the cable being pinched
  or needing to bend sharply right at the enclosure wall.
- **BOOT button access**: see above — must be reachable from outside.
- **Internal depth budget**: leave a few cm of depth behind the panel for
  the driver board, the FPC cable's bend, and USB cable strain relief —
  don't design this paper-thin. A rough figure to work with: 25-35mm
  internal depth is probably enough, but this should flex based on your
  actual layout once real dimensions are confirmed.
- **No cooling needed** — e-paper and an ESP32 at idle produce negligible
  heat. Ventilation is not a design driver here.
- **No moving parts other than the BOOT button** — no hinges, no battery
  door (not battery powered).
- **Printable on a consumer FDM printer**: assume a build volume around
  220×220×250mm (a common "Ender/Prusa-class" printer bed) unless you have
  reason to think otherwise. The panel (~194mm) is close to that limit, so
  either the design needs to fit flat within that footprint, or the
  enclosure is split into multiple printed parts (e.g. a front bezel + a
  back shell, or a frame + a separate stand) that assemble together —
  which is very normal for a piece this size, so don't force a single-part
  print if it doesn't fit well.
- **Assembly approach**: prefer screws (self-tapping into printed bosses,
  or heat-set inserts) or a snap-fit over glue, since the electronics may
  need to come back out for a re-flash or a revision. Mention your assumed
  assembly method for each concept.

## Aesthetic direction — explore a few distinct options, don't converge on one

This is a personal desk gadget with a space/satellite-tracking theme.
Some directions worth exploring (propose your own too, these are just
starting points):

1. **Minimalist / Scandinavian desk object** — light wood-tone or matte
   white/black plastic, plain rectangular frame, thin bezel, quiet and
   "furniture-like" rather than "gadget-like." Would look at home next to
   a MUJI alarm clock.
2. **Retro-futuristic "mission control" panel** — dark bezel, visible
   fasteners or rivets as a design feature, maybe a small recessed area or
   plate around the BOOT button that reads like a labeled control, aviation
   / NASA-console inspired.
3. **Digital-photo-frame-on-an-easel** — a slim frame that leans back on
   an integrated fold-out or fixed easel-style leg, echoing a picture frame
   rather than a "device."
4. **Visible-industrial / utilitarian** — exposed screw heads, a visible
   parting line between front and back shells, functional-first look,
   like a piece of test equipment or a Raspberry Pi case — deliberately
   not trying to hide that it's 3D printed.

For each concept you propose, include:
- A short description of the look and why it fits (or intentionally
  contrasts with) the "satellite tracker" theme.
- How it's likely split into printed parts, and how they'd assemble
  (screws/inserts/snap-fit).
- Where the screen cutout, BOOT button access, USB exit, and
  stand/kickstand live on that specific shape.
- A rendered image or clear enough sketch/description that someone could
  start blocking out the shape in CAD from it (front view, 3/4 angled
  view showing the stand, and a back view showing the cable/button access
  are the most useful angles if you can produce images).

## Out of scope for this brief

- Actual parametric CAD/STEP/STL files — that's the next step, done by a
  human in real CAD software, using these concepts as a starting point.
- Exact final dimensions — flag where you're guessing vs. where you pulled
  a real spec, but don't block on having millimeter-perfect numbers before
  proposing a direction.
- Electronics changes — the PCBs and their button/port positions are fixed
  inputs to this brief, not something to redesign.
