# Campaign time and native script scheduling

Protocol 32 normalizes native DateTime arithmetic using 64-bit intermediate
milliseconds, retaining the native two-u32 day/millisecond representation.
Exactly 86,400,000 milliseconds carries into the next day. Addition carries
before a u32 sum can wrap; subtraction borrows whole days and clamps at zero.
The arithmetic helper saturates the representable DateTime maximum. The shared
story codec retains its existing signed-seconds current-clock limit; this batch
does not change saved or wire clock layouts.

Native IN script delays keep their factor of eight and their minimum eight-ms
tick. AT scheduling uses the current game time of day, with a minimum one-ms tick
when the requested second matches. It no longer subtracts the campaign's initial
hour and schedules at the wrong daily hour. Negative/zero relative script delays
use the minimum tick. Long relative delays multiply in 64 bits before splitting
into days, rather than overflowing during seconds-to-ms or the IN multiplier.
Teleport time costs, script fades and delayed fade-in use the same conversion.
Script-created TeleportData is fully initialized, including movie flags/colors.

The host executes scheduled native callbacks; replicas receive authoritative
clocks and their resulting object/story changes. Shared presentation freezes game
and animation clocks while pumping transport and anchoring native real time.
Sea-journey time is charged once by the host after its outgoing presentation.
Delayed native NPC relocation keeps its original saved triggerer/attachee IDs.

The portable time-math fixture covers exact midnight, borrowing, saturation,
long scaled delays, a non-midnight campaign origin and a 50,000-case arithmetic
corpus. The combined voyage fixture schedules native IN/AT callbacks for a remote
owner, checks that shared fades do not advance their clock, then requires their
owner-specific stat results. It also checks the original Teach voyage's time
cost and delayed captain relocation. See `history/CAMPAIGN_PROGRESS.md` for the accepted
native scope and remaining campaign work.
