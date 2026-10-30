# Party documents

Protocol 34 adds a party document catalogue. Authoritative native reads retain
the written type, message/art number and newspaper variant. Schematics remain
personal learning operations. Rereading a discovered document through F7 opens
the native reading UI locally, without invoking its item or quest script again.
Players retain independent page navigation; the catalogue does not pause others.

The menu uses the native fonts and a nine-entry page, with mouse/number selection
and keyboard paging. Titles prefer native written-item descriptions, including
loaded prototypes, and otherwise use installed message text. Plain newspaper
headlines retain their first letter; book page/image directives are not titles.
New native conversations or personal reading presentations close the menu. It is available
only during live gameplay outside an active conversation.

`ACD1` stores magic, big-endian u32 record count, sorted eight-byte records and a
CRC32. Records contain type and variant bytes, two reserved zero bytes and a
big-endian u32 message number. Types 0–6 except schematic 5 are valid; only
newspapers permit variant 1. Numbers are bounded below INT32_MAX minus 10,000.
Duplicate/out-of-order records, reserved bytes, malformed lengths and bad CRCs
are rejected before replacing state. Limits are 2,048 documents / 16,396 bytes.

`ACW5` adds a document byte count at offset 28 to its 32-byte world header. Its
body is objects, deletions, optional story, sectors, exploration and documents.
The catalogue changes independently of story and map exploration. The initial
view includes an empty catalogue. Native installation validates every referenced
text/art against installed content before applying a received catalogue.

Native saves include `arcmp-documents.bin`; snapshot validation checks its codec
before extraction. Legacy saves without the sidecar start with an empty library.
Load, travel and rejoin replace the catalogue with the campaign's saved state.

The consolidated protocol-34 portable matrix passes 29 checks on each of Linux,
ASan/UBSan and Windows/Wine (87 total). The document corpus covers sorted and
duplicate additions, type/variant bounds, full capacity, malformed lengths,
order/reserved-byte rejection with recomputed CRCs and 10,000 mutated inputs
with canonical round trips. The native sanitizer object oracle rejects a structurally valid
catalogue referencing missing installed content before changing objects/story.
Logs are retained under `evidence/dark-checks`. Linux-host native route prefixes
cover stages 97–123 through story 15, saving/loading three documents and then
all four owners rereading the authentic quest book through F7 without reverting
story 15. The native library, paper and book views are captured for visual review.
Windows-host stages 97–123 pass in one connected run, ending with three identical
documents, 357 identical journal pages and matching native state/archives. A
focused final library check verifies corrected native item titles and plain
newspaper headlines; it retains the accepted chapter run.
