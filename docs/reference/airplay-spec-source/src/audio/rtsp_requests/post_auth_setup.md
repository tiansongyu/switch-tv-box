# POST /auth-setup

In case MFi authentication is supported (HasUnifiedAdvertiserInfo), it can be raised either with a auth-setup challenge or included in the pairing process if supported (SupportsUnifiedPairSetupAndMFi).
This section describes the first option.

Note that even if it is a server authentication (so that clients ensure MFi authenticity), with Airplay2 devices this step cannot be ignored from a client implementation point of view. Meaning, even if the
authentication/signature is not checked on client side, the request has to be done, otherwise the server will deny further requests

The challenge process is the following:
1. Generation of Curve25119 key pairs on both client and server
2. Client send its public key to server
3. Server append its public key with client's one, this will be the message to sign. It then gets the signature from Apple authentication IC. RSA-1024 is used, with SHA-1 hash algorithm.
4. Signature is encrypted with AES-128 in Counter mode with:
	- Key = 16 first bytes of `SHA1(<7:AES-KEY><32:Curve25119 Shared key>)` 
	- IV  = 16 first bytes of `SHA1(<6:AES-IV><32:Curve25119 Shared key>)` 
5. Server respond with it's Curve25119 public key, the encrypted signature and the certificate.

A pair-setup request body has the following format:

`<1:Encryption Type>`  
`<32:Client’s Curve25119 public key>`

Where encryption type is:

|value     |type                        |
|----------|----------------------------|
|0x00      |Invalid                     |
|0x01      |Unencrypted                 |
|0x10      |MFi-SAP-encrypted AES key.  |

A pair-setup response body has the following format:

```
<32:Server’s Curve25119 public key>  
<4:Certificate length (int32be)>  
<n:PKCS#7 DER encoded MFiCertificate>  
<4:Signature length (int32be)>  
<n:Signature>
```
<div class="client_server">
<p>client &rarr; server</p>

```http
POST /auth-setup RTSP/1.0
Content-Type: application/octet-stream
Content-Length: 33
CSeq: 1
User-Agent: iTunes/7.6.2 (Windows; N;)
Client-Instance: 050748FD6F0853AC
DACP-ID: 124D322761E2EC16
Client-instance-identifier: 5743a6b4-e835-412f-9894-14011e386cf2

<BINARY_REQUEST>
```
</div>
<div class="server_client">
<p>server &rarr; client</p>

```http
RTSP/1.0 200 OK
Content-Length: 1076
Content-Type: application/octet-stream
Server: AirTunes/366.0
CSeq: 1

<BINARY_RESPONSE>
```
</div>
