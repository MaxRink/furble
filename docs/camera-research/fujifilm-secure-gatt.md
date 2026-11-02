# Fujifilm Secure GATT reference

Scope: Fujifilm X100VI 1.31 and X-S20 3.30. The two images carry the
same 70-entry ASCII UUID table, 13 services, 69 characteristics, and 21
CCCDs. The UUID set and order are `firmware-static`. Handle, role,
properties, and maximum length are `public-source` from the MIT fffw
`ad14d4` profile and match the firmware record set. Binding one UUID to
one handle is therefore an inference from the exact set match.

The vendor images are not redistributed. This table is a compatibility
reference, not a promise of runtime reachability or physical capture.

Link security entries below are unverified assumptions from the public profile
and XApp behaviour, not decoded per-characteristic firmware permission checks.
Treat them as a hypothesis until a firmware permission check or a hardware
capture establishes the requirement.

## Services

| Handle | UUID | Role | Characteristics | Link security (assumed, unverified) |
| --- | --- | --- | --- | --- |
| `0x0001` | `00001800-0000-1000-8000-00805f9b34fb` | Generic Access | 2 read | none, standard |
| `0x1001` | `00001801-0000-1000-8000-00805f9b34fb` | Generic Attribute | 1 read plus CCCD | none, standard |
| `0x1101` | `0000180A-0000-1000-8000-00805f9b34fb` | Device Information | 6 read | none, standard |
| `0x2001` | `A9D2B304-E8D6-4902-8336-352B772D7597` | Camera information, XApp name `CAMERA_INFORMATION_RED` | 7 read | bonded link |
| `0x3001` | `15CA59FE-620C-464D-A987-223FAB660CDE` | Lens information | 3 read | bonded link |
| `0x4001` | `123D8F06-62A1-4935-9322-833C531EE225` | Connected device information, XApp name `CONNECTED_DEVICE_INFORMATION_RED` | 7, mixed read/write | bonded link; status read is the pairing gate |
| `0x4501` | `4E941240-D01D-46B9-A5EA-67636806830B` | Camera settings, XApp name `SERVICE_FF_CAMERA_SETTING` | 10, mostly read/write/notify | bonded link |
| `0x5001` | `4C0020FE-F3B6-40DE-ACC9-77D129067B14` | Camera state, XApp name `SERVICE_FF_CAMERA_STATE` | 12, read plus indicate/notify | bonded link |
| `0x6001` | `6514EB81-4E8F-458D-AA2A-E691336CDFAC` | Shutter and control | 9 write-only | bonded link, registration required |
| `0x7001` | `E872B11F-D526-4AE1-9BB4-89A99D48FA59` | Current time, XApp name `SERVICE_FF_CURRENT_TIME` | 2 write-only | bonded link |
| `0x8001` | `3B46EC2B-48BA-41FD-B1B8-ED860B60D22B` | Current location, XApp name `SERVICE_FF_CURRENT_LOCATION` | 1 write-only, 23 bytes | bonded link |
| `0x9001` | `AF854C2E-B214-458E-97E2-912C4ECF2CB8` | Equipment status and backup, also the legacy Basic CR advertised UUID | 7, read/write/notify | bonded link on Secure firmware |
| `0xA001` | `804DAA8E-FFEB-4AB3-8E75-6EDD7303208D` | Camera startup information, XApp name `CAMERA_STARTUP_INFORMATION_RED` | 2 read/notify and read/write/notify | bonded link |

## UUID table

| # | UUID | Handle and role | Properties | Max | Furble |
| --- | --- | --- | --- | --- | --- |
| 0 | `FB15C357-364F-49D3-B5C5-1E32C0DED038` | `0x6004` SHUTTER, unknown | write | 1 | - |
| 1 | `A68E3F66-0FCC-4395-8D4C-AA980B5877FA` | `0x5002` STATE, AP state, IND1 | read, indicate | 2 | used |
| 2 | `F14E7A88-6F57-4872-A69B-B47EF3295C9B` | `0x6010` SHUTTER, unknown | write | 2 | - |
| 3 | `11438C83-CFB0-4511-841B-759E0D2321C8` | `0x5017` STATE, backup state | read, indicate | 2 | - |
| 4 | `389363E4-712E-4CF2-A72E-BFCF7FB6ADC1` | `0x2006` INFO, camera info | read | 2 | - |
| 5 | `BF6DC9CF-3606-4EC9-A4C8-D77576E93EA4` | `0x4502` SETTING, camera name or SSID, NOT4 | read, write, notify | 32 | used |
| 6 | `E8E40D50-A625-4F1D-96ED-8CEC034F5690` | `0x200C` INFO, camera info | read | 10 | - |
| 7 | `E6692C5C-B7CD-44F4-95FC-EDA07CE32560` | `0x5014` STATE, camera vital state, NOT6 | read, notify | 2 | used |
| 8 | `27870478-94A9-4345-849B-EFA3BF37887F` | `0x200A` INFO, camera info | read | 30 | - |
| 9 | `34D8C8DE-E2A9-43FF-822C-7D945DD8D8E1` | `0x501D` STATE, unknown | read | 2 | - |
| 10 | `49A12959-DFAA-4EB2-89CE-62548AD948F3` | `0x200E` INFO, camera info | read | 17 | - |
| 11 | `CAE8C738-4BB6-4605-98C1-58331C87F3B6` | `0x2008` INFO, camera info | read | 1 | - |
| 12 | `EB4166B0-9CCA-445E-A4E4-75B3817FD57A` | `0x400C` PAIR, unknown | write | 2 | - |
| 13 | `7EDE1988-B27E-43FC-80F4-6FEC994F0552` | `0x4006` PAIR, disconnect reason | read, write | 2 | - |
| 14 | `F557D96B-8284-4667-8793-B971C1DECA2A` | `0x400E` PAIR, status or device ID | read, write | 4 | used |
| 15 | `85B9163E-62D1-49FF-A6F5-054B4630D4A1` | `0x4002` PAIR, client name | write | 30 | used |
| 16 | `A80BE3F8-8BCB-4ADD-A725-170B7A53ADC9` | `0x4004` PAIR, unknown | write | 1 | - |
| 17 | `763514E8-C627-4B9E-B7E8-89074AD3CCC7` | `0x400A` PAIR, unknown | write | 2 | - |
| 18 | `F9150137-5D40-4801-A8DC-F7FC5B01DA50` | `0x5008` STATE, date sync state, NOT1 | read, notify | 2 | used |
| 19 | `B9BFD37F-CCAD-4D36-A1EE-018E792B3EDF` | `0x7002` TIME, local date and time | write | 7 | - |
| 20 | `20EA1FAC-19D8-48A8-A80C-535D34300958` | `0x501F` STATE, unknown status, 29 bytes | read | 29 | - |
| 21 | `B1307521-7AC5-4199-AAEE-9D094781CE69` | `0x6008` SHUTTER, unknown, 92 bytes | write | 92 | - |
| 22 | `049EC406-EF75-4205-A390-08FE209C51F0` | `0x500E` STATE, firmware update state, IND3 | read, indicate | 2 | used |
| 23 | `C922AC69-9480-4348-8F4B-9EE29BC30D1D` | `0x9006` STATUS, backup file name or info | read, write | 44 | - |
| 24 | `E3FBBFCF-F326-4B0F-82CF-B00AE1B107A2` | `0x9002` STATUS, unknown | read | 4 | - |
| 25 | `AC0C799A-FA6C-4DF5-BBC5-BB95CCE7E6EA` | `0x900A` STATUS, backup file data | read, write | 248 | - |
| 26 | `7F3400FE-17E7-4B80-8A0E-81B0343C1B49` | `0x9008` STATUS, unknown | read, write | 4 | - |
| 27 | `2E27ED9F-5506-41CD-BA48-DAC06669AD95` | `0x900E` STATUS, backup notify | notify | 4 | - |
| 28 | `051DD980-DF9D-4472-A2E1-35811DD24EE1` | `0x9004` STATUS, unknown | write | 4 | - |
| 29 | `68052E8A-FB91-404F-8847-0EB4BE24308C` | `0x900C` STATUS, backup mode setting | write | 2 | - |
| 30 | `600655E6-3637-42F1-8FB2-44EFC5C63B13` | `0x6002` SHUTTER, WiFi launch trigger | write | 2 | - |
| 31 | `82A9F452-C5CE-4EF5-8203-3FC9A47F8171` | `0x451B` SETTING, image resize setting, NOT9 | read, write, notify | 1 | used |
| 32 | `CAEDB497-83BF-482C-91EF-91CF6F1216FF` | `0x4515` SETTING, image transfer setting | read, write, notify | 1 | - |
| 33 | `98934B2C-756C-4632-AA2F-DCBA1BFEC824` | `0x4518` SETTING, image transfer setting ex | read, write, notify | 4 | used |
| 34 | `2674F0B0-F909-4E73-8656-338E9C74457F` | `0x600E` SHUTTER, unknown | write | 2 | - |
| 35 | `2F6CB772-1D69-448E-8819-FE6B5C20B094` | `0x5011` STATE, log transfer state | read, indicate | 2 | - |
| 36 | `621A98D8-6314-4C3E-A683-EEA1D6166606` | `0x3004` LENS, lens firmware version | read | 5 | - |
| 37 | `68887A7A-10A7-4A4A-AADD-6F9DB1ABAA0E` | `0x3002` LENS, lens name | read | 64 | - |
| 38 | `DF287E0C-C23F-4F80-AFDD-C2B570288CDB` | `0x3006` LENS, lens info | read | 10 | - |
| 39 | `0F36EC14-29E5-411A-A1B6-64EE8383F090` | `0x8002` LOCATION, geotag packet | write | 23 | used |
| 40 | `C95D91AE-B247-4D6D-8661-7DD5D6A0F85B` | `0x450F` SETTING, location sync cycle | read, write, notify | 2 | used |
| 41 | `AD06C7B7-F41A-46F4-A29A-712055319122` | `0x500B` STATE, location sync state or geotag request | read, notify | 2 | used |
| 42 | `AAB609C4-94DD-4D89-BC60-665D5090B828` | `0x450C` SETTING, location sync setting, NOT7 | read, write, notify | 2 | used |
| 43 | `75823784-FBB7-4B71-ABAE-CD9A34072E3C` | `0x4512` SETTING, logging setting, NOT5 | read, write, notify | 13 | used |
| 44 | `861442AB-B94E-4935-90D9-41E291D91374` | `0x600A` SHUTTER, unknown | write | 2 | - |
| 45 | `ABA356EB-9633-4E60-B73F-F52516DBD671` | `0x4008` PAIR, legacy pairing key | write | 4 | used |
| 46 | `8814441B-1D7B-4046-891D-D8F80864CC8E` | `0x450A` SETTING, unknown | read | 2 | - |
| 47 | `43070F6C-51E0-4887-86A7-5F762BDA5791` | `0x600C` SHUTTER, power control request | write | 2 | - |
| 48 | `F90F7D3A-3B64-45C6-AB21-933900184837` | `0xA002` STARTUP, power key state | read, notify | 2 | - |
| 49 | `7170FD5A-56D9-4C19-B043-7A7047D8E1A0` | `0xA005` STARTUP, remote boot setting | read, write, notify | 2 | - |
| 50 | `BD45F887-A6BE-4CB7-8565-390DF38BF5BF` | `0x4507` SETTING, reservation after shooting | read, write, notify | 2 | - |
| 51 | `F1897EB9-A9AA-40F0-8CE6-2FA7915BC459` | `0x6012` SHUTTER, unknown | write | 2 | - |
| 52 | `4B3A413C-230F-42BC-B3ED-B1DB2EADEE82` | `0x501A` STATE, restore state | read, indicate | 2 | - |
| 53 | `7FCF49C6-4FF0-4777-A03D-1A79166AF7A8` | `0x6006` SHUTTER, shutter and focus | write | 2 | used |
| 54 | `4BF16F6D-7264-4E76-B0D2-52CDE16D2C7D` | `0x2004` INFO, camera info | read | 4 | - |
| 55 | `1587B102-0B6D-4B63-9226-66FCC6D17387` | `0x5021` STATE, unknown | read | 2 | - |
| 56 | `BD17BA04-B76B-4892-A545-B73BA1F74DAE` | `0x5005` STATE, transfer state, IND2 | read, indicate | 2 | used |
| 57 | `C52EDBCE-1FE2-4ECC-9483-907E6592BE9E` | `0x7004` TIME, UTC and timezone | write | 12 | - |
| 58 | `9C72C205-5740-4F17-9949-0D3FADF2F67A` | `0x2002` INFO, camera info | read | 2 | - |
| 59 | `E809256A-915C-4967-92E8-53B7D4CAD213` | `0x4505` SETTING, WiFi password | read | 20 | - |
| 60 | `6514EB81-4E8F-458D-AA2A-E691336CDFAC` | service `0x6001` SHUTTER | - | - | used |
| 61 | `A9D2B304-E8D6-4902-8336-352B772D7597` | service `0x2001` INFO | - | - | used |
| 62 | `4E941240-D01D-46B9-A5EA-67636806830B` | service `0x4501` SETTING | - | - | used |
| 63 | `804DAA8E-FFEB-4AB3-8E75-6EDD7303208D` | service `0xA001` STARTUP | - | - | used |
| 64 | `4C0020FE-F3B6-40DE-ACC9-77D129067B14` | service `0x5001` STATE | - | - | used |
| 65 | `123D8F06-62A1-4935-9322-833C531EE225` | service `0x4001` PAIR | - | - | used |
| 66 | `3B46EC2B-48BA-41FD-B1B8-ED860B60D22B` | service `0x8001` LOCATION | - | - | used |
| 67 | `E872B11F-D526-4AE1-9BB4-89A99D48FA59` | service `0x7001` TIME | - | - | - |
| 68 | `AF854C2E-B214-458E-97E2-912C4ECF2CB8` | service `0x9001` STATUS | - | - | used |
| 69 | `15CA59FE-620C-464D-A987-223FAB660CDE` | service `0x3001` LENS | - | - | - |

## Furble notes

- `A9D2B304`, `123D8F06`, `F557D96B`, `85B9163E`, `4C0020FE`,
  `6514EB81`, `7FCF49C6`, `3B46EC2B`, and `0F36EC14` are the Secure
  path used by `FujifilmSecure`.
- `91F1DE68` is absent. Basic token pairing cannot complete on these
  firmware versions; `AF854C2E` is the equipment status and backup
  service here.
- `2A125640` and `DEEF7187` are absent. Optional subscriptions must stay
  non-fatal.
- `7170FD5A` is the remote boot setting in the startup information
  service, not a generic notification.
- `F9150137` `01 00` is also a date or time request in the official apps.
  Furble uses it as an operational promotion, not a proven registration
  flag.
- Furble answers the `F9150137` `01 00` request with the 12-byte UTC packet on
  `C52EDBCE`, offset zero and DST off. The 7-byte local form on `B9BFD37F` is
  not written: it expects wall-clock time and Furble has no timezone source, so
  a body without a writable UTC characteristic keeps its clock unchanged. The
  physical clock effect is capture-gated.

## Sources

- Fujifilm X100VI 1.31 firmware: https://dl.fujifilm-x.com/support/firmware/x100vi-131-a7wr26ib/FPUPDATE.DAT
- Fujifilm X-S20 3.30 firmware: https://dl.fujifilm-x.com/support/firmware/x-s20-330-zv07xe22/FWUP0031.DAT
- tiredboffin/fffw profile and GATT template: https://github.com/tiredboffin/fffw/tree/ccee949884a6dad10095231441e4d608c10e59f0/ffbt/cfg
- sarmadjari/GeoShutter X100VI protocol notes: https://github.com/sarmadjari/GeoShutter/tree/b8e2dd3443b0c3740861f6d4beeac56101bd3686/docs
- petabyt/libfuji: https://github.com/petabyt/libfuji/tree/3328efc51f74d754f8fa76af70c6becdde62271f
