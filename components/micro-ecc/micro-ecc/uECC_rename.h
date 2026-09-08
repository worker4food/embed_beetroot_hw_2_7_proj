/* Not part of upstream micro-ecc: ESP-IDF's "bt" component links in its own
 * tinycrypt-derived ecc.c/ecc_dh.c (used for BLE LE Secure Connections
 * pairing, secp256r1 only), which exports global symbols under these exact
 * uECC_* names. Since that component is unconditionally part of this build,
 * linking a second, independent uECC.c would collide with it at link time.
 * Renaming this copy's public symbols avoids the clash without touching the
 * bt component or losing secp160r1 support (which tinycrypt's copy lacks). */
#pragma once

#define uECC_set_rng river2_uECC_set_rng
#define uECC_get_rng river2_uECC_get_rng
#define uECC_curve_private_key_size river2_uECC_curve_private_key_size
#define uECC_curve_public_key_size river2_uECC_curve_public_key_size
#define uECC_make_key river2_uECC_make_key
#define uECC_shared_secret river2_uECC_shared_secret
#define uECC_compress river2_uECC_compress
#define uECC_decompress river2_uECC_decompress
#define uECC_valid_public_key river2_uECC_valid_public_key
#define uECC_compute_public_key river2_uECC_compute_public_key
#define uECC_sign river2_uECC_sign
#define uECC_sign_deterministic river2_uECC_sign_deterministic
#define uECC_verify river2_uECC_verify
#define uECC_secp160r1 river2_uECC_secp160r1
#define uECC_secp192r1 river2_uECC_secp192r1
#define uECC_secp224r1 river2_uECC_secp224r1
#define uECC_secp256r1 river2_uECC_secp256r1
#define uECC_secp256k1 river2_uECC_secp256k1
