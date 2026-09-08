# EcoFlow River 2 Pro: BLE Protocol Reference

Source: [rabits/ha-ef-ble](https://github.com/rabits/ha-ef-ble). River 2, River 2 Max,
and River 2 Pro share one identical protocol, distinguished only by serial-number
prefix (§1).

---

## 1. Device identification

- Manufacturer key: **`0xB5B5`**. Manufacturer-data layout:

  | Offset | Field |
  |---|---|
  | `[0]` | `proto_version` |
  | `[1:17]` | `serial_number` (ASCII, null-stripped) |
  | `[17]` | `status` |
  | `[18]` | `product_type` |
  | `[22]` | `capability_flags` (only present if data is longer than 19 bytes; otherwise treat as `0b0111000`) |

- `capability_flags` bits 3-5 (`0x38`) give `encrypt_type = (flags & 0x38) >> 3`, which
  selects the handshake path in §3. Default (flags absent) → `encrypt_type = 7`, the
  path River2-family devices use.

- Model match: first 4 ASCII bytes of `serial_number`:

  | Serial prefix | Model |
  |---|---|
  | `R601`, `R603` | River 2 |
  | `R611`, `R613` | River 2 Max |
  | `R621`, `R623` | River 2 Pro |

- Wire/packet version: **2** (see §4).

---

## 2. Transport (BLE characteristics)

Try both characteristic pairs against the connected device's GATT table; use whichever
is present:

| Variant | Notify | Write |
|---|---|---|
| rfcomm-style | `00000003-0000-1000-8000-00805f9b34fb` | `00000002-0000-1000-8000-00805f9b34fb` |
| Nordic UART | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |

Flow: connect → resolve both characteristics → subscribe to notifications → run
handshake (§3) → continue reading notifications for telemetry/replies. One GATT write
per outbound frame (the BLE stack handles ATT fragmentation). A logical frame can arrive
split across multiple notifications. Buffer and rescan for a valid frame prefix (§4)
as bytes accumulate.

---

## 3. Encryption & handshake (`encrypt_type = 7`)

Cipher: **AES-128-CBC, PKCS7 padding**. When decrypting, truncate ciphertext to a
multiple of 16 bytes first, then decrypt and unpad. Fall back to the raw decrypted
bytes if unpadding fails.

1. **ECDH key exchange.** Generate an ephemeral EC keypair on curve **SECP160r1**. Send
   `b"\x01\x00" + our_public_key_bytes` as an unencrypted command frame (§4.1 variant).
   Read the reply; byte `[2]` selects a key size:
   ```
   curve_type 1       -> 52 bytes
   curve_type 2       -> 56 bytes
   curve_type 3 or 4  -> 64 bytes
   anything else      -> 40 bytes
   ```
   Device public key = reply bytes `[3 : 3+size]`. Compute the ECDH shared secret from
   your private key and the device's public key. `iv = MD5(shared_secret)`. Interim
   cipher: AES-CBC with `key = shared_secret[:16]`, this `iv`.

2. **Session-key request.** Send byte `0x02` as an unencrypted command frame. Decrypt
   the reply with the interim cipher (skip the reply's leading type byte first). Result
   is `sRand = decrypted[0:16]`, `seed = decrypted[16:18]`. Derive the final key:
   ```
   pos = seed[0] * 0x10 + ((seed[1] - 1) & 0xFF) * 0x100
   n0 = u64_le(lookup_table[pos    : pos+8])
   n1 = u64_le(lookup_table[pos+8  : pos+16])
   n2 = u64_le(sRand[0:8])
   n3 = u64_le(sRand[8:16])
   session_key = MD5(pack_le_u64(n0) + pack_le_u64(n1) + pack_le_u64(n2) + pack_le_u64(n3))
   ```
   `lookup_table` is a static ~84 KB opaque binary blob; obtain its exact bytes from the
   reference implementation linked above. Final cipher: AES-CBC with `key =
   session_key`, same `iv` as step 1.

3. **Auth-status wake-up.** Under the final session key, send an inner packet (§4.2):
   `src=0x21, dst=0x35, cmd_set=0x35, cmd_id=0x89, dsrc=1, ddst=1, payload=b""`. Wait for
   a reply with `src == 0x35, cmd_set == 0x35`.

4. **Auto-authentication.** Obtain `user_id` via EcoFlow cloud login (`POST` to the
   regional `/auth/login` endpoint, base64-encoded password), done once, outside the BLE
   protocol. Payload = uppercase-hex ASCII of `MD5(user_id + serial_number)` (32
   characters). Send as an inner packet: `src=0x21, dst=0x35, cmd_set=0x35,
   cmd_id=0x86, dsrc=1, ddst=1`.

5. Check the reply payload against known auth-error codes; any non-error reply (or the
   next data packet) marks authentication complete.

---

## 4. Packet framing

### 4.1 Outer wrapper (crosses BLE write/notify characteristics)

```
[0:2]   0x5A5A prefix
[2]     (frame_type << 4)   -- 0x00 = command/handshake, 0x01 = data
[3]     0x01
[4:6]   u16 LE length = len(encrypted_payload) + 2
[6:-2]  AES-128-CBC(payload, key=session_key, iv=session_iv), PKCS7-padded
[-2:]   CRC16 LE over bytes [0:-2]
```
`payload` is a complete inner packet (§4.2), serialized then encrypted.

**Unencrypted command frame** (handshake steps 1-2, before a session key exists): same
layout with `frame_type = 0` and `payload` sent as-is (no encryption).

### 4.2 Inner packet (wire version 2)

```
[0]      0xAA prefix
[1]      0x02
[2:4]    payload_length, u16 LE
[4]      CRC8 over bytes [0:4]
[5]      0x0D
[6:10]   seq (4 bytes; opaque passthrough)
[10:12]  0x00 0x00
[12]     src
[13]     dst
[14]     cmd_set
[15]     cmd_id
[16:]    payload (payload_length bytes)
[-2:]    CRC16 LE over the frame minus these 2 bytes
```
Validate: `0xAA` prefix, CRC8 over `[0:4]` == byte `[4]`, CRC16 over all but the last 2
bytes == those last 2 bytes (LE).

### 4.3 CRC algorithms

| | Width | Poly | Init | Reflected | Applied to |
|---|---|---|---|---|---|
| CRC8 | 8 | `0x07` | `0x00` | no | inner-packet bytes `[0:4]` |
| CRC16 | 16 | `0x8005` | `0x0000` | yes (in/out) | full outer wrapper and full inner packet, each minus their trailing 2 CRC bytes |

```
CRC8 table-build:
  for i in 0..255:
    crc = i
    repeat 8: crc = ((crc<<1)^0x07)&0xFF if (crc&0x80) else (crc<<1)&0xFF
    table[i] = crc
CRC8(data): crc=0x00; for b in data: crc = table[crc ^ b]; return crc

CRC16 table-build:
  for i in 0..255:
    crc = i
    repeat 8: crc = (crc>>1)^0xA001 if (crc&1) else (crc>>1)
    table[i] = crc
CRC16(data): crc=0x0000; for b in data: crc = table[(crc^b)&0xFF] ^ (crc>>8); return crc
```

### 4.4 Board addresses (`src`/`dst`)

| Value | Board |
|---|---|
| `0x02` | PD (main/power-delivery controller) |
| `0x03` | EMS / BMS |
| `0x04` | INV (inverter) |
| `0x05` | MPPT (solar/DC charge controller) |
| `0x21` | App/phone (always `src` on outbound commands) |
| `0x35` | Auth/handshake responder |

---

## 5. Telemetry messages (device → app)

Each payload is a fixed-width little-endian struct decoded strictly in field order
(concatenate each field's format character into one struct-unpack format string). If a
payload is shorter than the full struct, decode as many leading fields as fit and treat
the rest as absent.

Type legend: `B`=uint8, `b`=int8, `H`=uint16 LE, `h`=int16 LE, `I`=uint32 LE, `i`=int32
LE, `f`=float32 LE, `Ns`=raw N-byte blob.

Dispatch by `(src, cmd_set, cmd_id)`:

| `src` | `cmd_set` | `cmd_id` | Message |
|---|---|---|---|
| `0x02` | `0x20` | `0x02` | PD heartbeat (§5.1) |
| `0x03` | `0x20` | `0x02` | EMS heartbeat (§5.2) |
| `0x03` | `0x20` | `0x32` | BMS heartbeat (§5.3) |
| `0x04` | any | `0x02` | Inverter heartbeat (§5.4) |
| `0x05` | `0x20` | `0x02` | MPPT heartbeat (§5.5) |

### 5.1 PD heartbeat

| # | Field | Type |
|---|---|---|
| 1 | model | `B` |
| 2 | err_code | `4s` |
| 3 | sys_ver | `4s` |
| 4 | wifi_ver | `4s` |
| 5 | wifi_auto_rcvy | `B` |
| 6 | soc | `B` |
| 7 | watts_out_sum | `H` |
| 8 | watts_in_sum | `H` |
| 9 | remain_time | `i` |
| 10 | beep_mode | `B` |
| 11 | dc_out_state | `B` |
| 12 | usb1_watts | `B` |
| 13 | usb2_watts | `B` |
| 14 | qc_usb1_watts | `B` |
| 15 | qc_usb2_watts | `B` |
| 16 | typec1_watts | `B` |
| 17 | typec2_watts | `B` |
| 18 | typec1_temp | `B` |
| 19 | typec2_temp | `B` |
| 20 | car_state | `B` |
| 21 | car_watts | `B` |
| 22 | car_temp | `B` |
| 23 | standby_min | `H` |
| 24 | lcd_off_sec | `H` |
| 25 | lcd_brightness | `B` |
| 26 | chg_power_dc | `I` |
| 27 | chg_sun_power | `I` |
| 28 | chg_power_ac | `I` |
| 29 | dsg_power_dc | `I` |
| 30 | dsg_power_ac | `I` |
| 31 | usb_used_time | `I` |
| 32 | usbqc_used_time | `I` |
| 33 | typec_used_time | `I` |
| 34 | car_used_time | `I` |
| 35 | inv_used_time | `I` |
| 36 | dc_in_used_time | `I` |
| 37 | mppt_used_time | `I` |
| 38 | reserved | `2s` |
| 39 | ext_rj45_port | `B` |
| 40 | ext_3p8_port | `B` |
| 41 | ext_4p8_port | `B` |
| 42 | sys_chg_dsg_state | `B` |
| 43 | wifi_rssi | `B` |
| 44 | wireless_watts | `B` |
| 45 | screen_state | `14s` |
| 46 | ac_auto_config | `B` |
| 47 | min_ac_out_soc | `B` |
| 48 | ac_out_pause | `B` |
| 49 | watth_is_config | `B` |
| 50 | backup_soc | `B` |
| 51 | hysteresis_add | `B` |
| 52 | relay_switch_count | `I` |

### 5.2 EMS heartbeat

| # | Field | Type |
|---|---|---|
| 1 | chg_state | `B` |
| 2 | chg_cmd | `B` |
| 3 | dsg_cmd | `B` |
| 4 | chg_vol | `I` |
| 5 | chg_amp | `I` |
| 6 | fan_level | `B` |
| 7 | max_charge_soc | `B` |
| 8 | bms_model | `B` |
| 9 | lcd_show_soc | `B` |
| 10 | open_ups_flag | `B` |
| 11 | bms_warning_state | `B` |
| 12 | chg_remain_time | `I` |
| 13 | dsg_remain_time | `I` |
| 14 | ems_is_normal_flag | `B` |
| 15 | f32_lcd_show_soc | `f` |
| 16 | bms_is_connt | `3s` |
| 17 | max_available_num | `B` |
| 18 | open_bms_idx | `B` |
| 19 | para_vol_min | `I` |
| 20 | para_vol_max | `I` |
| 21 | min_dsg_soc | `B` |
| 22 | open_oil_eb_soc | `B` |
| 23 | close_oil_eb_soc | `B` |

### 5.3 BMS heartbeat

| # | Field | Type |
|---|---|---|
| 1 | num | `B` |
| 2 | type | `B` |
| 3 | cell_id | `B` |
| 4 | err_code | `I` |
| 5 | sys_ver | `I` |
| 6 | soc | `B` |
| 7 | vol | `I` |
| 8 | amp | `I` |
| 9 | temp | `B` |
| 10 | open_bms_idx | `B` |
| 11 | design_cap | `I` |
| 12 | remain_cap | `I` |
| 13 | full_cap | `I` |
| 14 | cycles | `I` |
| 15 | soh | `B` |
| 16 | max_cell_vol | `H` |
| 17 | min_cell_vol | `H` |
| 18 | max_cell_temp | `B` |
| 19 | min_cell_temp | `B` |
| 20 | max_mos_temp | `B` |
| 21 | min_mos_temp | `B` |
| 22 | bms_fault | `B` |
| 23 | bq_sys_stat_reg | `B` |
| 24 | tag_chg_amp | `I` |
| 25 | f32_show_soc | `f` |
| 26 | input_watts | `I` |
| 27 | output_watts | `I` |
| 28 | remain_time | `I` |

### 5.4 Inverter heartbeat

| # | Field | Type |
|---|---|---|
| 1 | err_code | `I` |
| 2 | sys_ver | `I` |
| 3 | charger_type | `B` |
| 4 | input_watts | `H` |
| 5 | output_watts | `H` |
| 6 | inv_type | `B` |
| 7 | inv_out_vol | `I` |
| 8 | inv_out_amp | `I` |
| 9 | inv_out_freq | `B` |
| 10 | ac_in_vol | `I` |
| 11 | ac_in_amp | `I` |
| 12 | ac_in_freq | `B` |
| 13 | out_temp | `H` |
| 14 | dc_in_vol | `I` |
| 15 | dc_in_amp | `I` |
| 16 | dc_in_temp | `H` |
| 17 | fan_state | `B` |
| 18 | cfg_ac_enabled | `B` |
| 19 | cfg_ac_xboost | `B` |
| 20 | cfg_ac_out_voltage | `I` |
| 21 | cfg_ac_out_freq | `B` |
| 22 | cfg_ac_work_mode | `B` |
| 23 | cfg_pause_flag | `B` |
| 24 | ac_dip_switch | `B` |
| 25 | cfg_fast_chg_watts | `H` |
| 26 | cfg_slow_chg_watts | `H` |
| 27 | standby_mins | `H` |
| 28 | discharge_type | `B` |
| 29 | ac_passby_auto_en | `B` |
| 30 | pr_balance_mode | `B` |
| 31 | ac_chg_rated_power | `H` |
| 32 | cfg_gfci_enable | `B` |

### 5.5 MPPT heartbeat

| # | Field | Type |
|---|---|---|
| 1 | fault_code | `I` |
| 2 | sw_ver | `4s` |
| 3 | in_vol | `I` |
| 4 | in_amp | `I` |
| 5 | in_watts | `H` |
| 6 | out_val | `I` |
| 7 | out_amp | `I` |
| 8 | out_watts | `H` |
| 9 | mppt_temp | `h` |
| 10 | xt60_chg_type | `B` |
| 11 | cfg_chg_type | `B` (0=AUTO, 1=SOLAR, 2=CAR) |
| 12 | chg_type | `B` |
| 13 | chg_state | `B` |
| 14 | dcdc_12v_vol | `I` |
| 15 | dcdc_12v_amp | `I` |
| 16 | dcdc_12v_watts | `H` |
| 17 | car_out_vol | `I` |
| 18 | car_out_amp | `I` |
| 19 | car_out_watts | `H` |
| 20 | car_temp | `h` |
| 21 | car_state | `B` |
| 22 | dc24v_temp | `h` |
| 23 | dc24v_state | `B` |
| 24 | chg_pause_flag | `B` |
| 25 | cfg_dc_chg_current | `I` (milliamps) |
| 26 | beep_state | `B` |
| 27 | cfg_ac_enabled | `B` |
| 28 | cfg_ac_xboost | `B` |
| 29 | cfg_ac_out_voltage | `I` |
| 30 | cfg_ac_out_freq | `B` |
| 31 | cfg_chg_watts | `H` |
| 32 | ac_standby_mins | `H` |
| 33 | discharge_type | `B` |
| 34 | car_standby_mins | `H` |
| 35 | power_standby_mins | `H` |
| 36 | screen_standby_mins | `H` |
| 37 | pay_flag | `H` |
| 38 | reserved | `8s` |

---

## 6. Commands (app → device)

Every command is an inner packet (§4.2) with `src=0x21`, wire version 2, `dsrc=1`,
`ddst=1`, wrapped per §4.1 under the session key.

| Action | `dst` | `cmd_set` | `cmd_id` | Payload |
|---|---|---|---|---|
| Enable/disable AC output | `0x05` | `0x20` | `0x42` | `[1\|0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF]` |
| Enable/disable AC X-Boost | `0x05` | `0x20` | `0x42` | `[0xFF, 1\|0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF]` |
| Enable/disable 12V DC output | `0x05` | `0x20` | `0x51` | `[1\|0]` |
| Enable/disable "AC always on" | `0x02` | `0x20` | `0x5F` | `[1\|0, 0x05]` |
| Enable/disable energy-backup mode | `0x02` | `0x20` | `0x5E` | `[1\|0, reserve_percent(0-100), 0x00, 0x00]` |
| Set energy-backup reserve level | `0x02` | `0x20` | `0x5E` | `[0x01, percent(0-100), 0x00, 0x00]` |
| Set minimum battery charge limit | `0x03` | `0x20` | `0x33` | 1 byte, percent (0-30) |
| Set maximum battery charge limit | `0x03` | `0x20` | `0x31` | 1 byte, percent |
| Set max DC charging current | `0x05` | `0x20` | `0x47` | `u32` = amps × 1000 (milliamps) |
| Set DC charge mode | `0x05` | `0x20` | `0x52` | 1 byte: `0=AUTO, 1=SOLAR, 2=CAR` |
| Set AC charging speed | `0x05` | `0x20` | `0x45` | `u16` watts + trailing `0xFF` byte |

### 6.1 Time sync

Answers a device's periodic time-sync request. This is the only payload in the protocol
that uses Protocol Buffers. Everything else is raw struct data:

- Message: single field `sys_utc_time` = current Unix timestamp (seconds), protobuf
  varint field.
- Inner packet: `src=0x21, dst=0x0B, cmd_set=0x01, cmd_id=0x55, dsrc=1, ddst=1`, wire
  version `0x13` (independent of the device's own protocol version).
- Throttle to at most once per 30 seconds.
