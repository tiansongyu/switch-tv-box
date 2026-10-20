# HomeKit Based Pairings

Default code is `3939`.


## Transient Pairing

`POST /pair-setup`
`EncryptionKey` is `SRP shared secret`

## Normal Pairing

`POST /pair-setup`
`POST /pair-verify`

`EncryptionKey` is agreed `SessionKey`

## Encryption

After successful pairing the connection switches to being encrypted using
the format `N:n_bytes:tag` where `N` is a 16 bit Little Endian length that
describes the number of bytes in `n_bytes` and `n_bytes` is encrypted using
ChaCha20-Poly1305 with `tag` being the Poly1305 tag.

Each direction uses its own key and nonce.

The key for data sent from client to accessory is a HKDF-SHA-512 with the
following parameters:

```
InputKey = <EncryptionKey>
Salt = ”Control-Salt”
Info = ”Control-Write-Encryption-Key”
OutputSize = 32 bytes
```

While the data sent from accessory to client is HKDF-SHA-512 with the following
parameters:

```
InputKey = <EncryptionKey>
Salt = ”Control-Salt”
Info = ”Control-Read-Encryption-Key”
OutputSize = 32 bytes
```

The nonce is a 64 bit counter (i.e. the high order bits of the full 96 bit
nonce is set to 0) starting with 0 and incrementing by 1 for each encrypted
block.

## MFi Authentication

When SupportsUnifiedPairSetupAndMFi is enabled and HKP is used, the device can authenticate when pairing.
In such case M1 step is done with "Pair Setup with Auth" method.

Also, during the M4 step of the pairing process, in addition of the PROOF TLV used in regular pair-setup, the following TLV is added:  
`TLV: 0x05,N,ENCRYPTED_DATA_WITH_TAG` where N (int16) is the length of ENCRYPTED_DATA_WITH_TAG

ENCRYPTED_DATA_WITH_TAG has the following format:  
`<N:ENCRYPTED_DATA>`  
`<16:Poly1305 Tag>`

ENCRYPTED_DATA is encrypted using a HKDF-SHA-512 key with the following parameters:
```
InputKey = <SRP Shared key>
Salt = ”Pair-Setup-Encrypt-Salt”
Info = ”Pair-Setup-Encrypt-Info”
Nonce = ”PS-Msg04”
OutputSize = 32 bytes
```
Decrypted data contains TLVs, which contain MFi Signature (signed by Apple authenticator IC) and used MFi certificate.
The message is signed using RSA-1024 with SHA-1 hash algorithm. The message signed is a HKDF-SHA-512 key with the following
parameters:
```
InputKey = <SRP Shared key>
Salt = ”MFi-Pair-Setup-Salt”
Info = ”MFi-Pair-Setup-Info”
OutputSize = 32 bytes
```