# AirPort Express Authentication

Sending audio data to the AirPort Express requires a *RSA* based
authentication. All binary data are encoded using [Base64] without padding.

## Client side

- In the `ANNOUNCE` request, the client sends a 128-bit random number in
  the `Apple-Challenge` header.
- A 128-bit *AES* key is generated, encrypted with the RSA public key
  using the *OAEP* encryption scheme, and sent along with an
  initialization vector in the `rsaaeskey` and `aesiv` SDP attributes.

## Server side

- The AirPort Express decrypts the AES key with its RSA private key, it
  will be used to decrypt the audio payload.
- The AirPort Express signs the `Apple-Challenge` number with its RSA
  private key using the *PKCS#1* signature scheme and send the result in
  the `Apple-Response` header.

## Client side

- The client decrypts the `Apple-Response` value with the RSA public key,
  and checks that it is the same random number it has previously generated.

<span class="ex">Example:</span> AirPort Express challenge/response

<div class="client_server">
<p>client &rarr; server</p>

```http
ANNOUNCE rtsp://10.0.1.101/3172942895 RTSP/1.0
CSeq: 1
Content-Type: application/sdp
Content-Length: 567
User-Agent: iTunes/4.6 (Windows; N)
Client-Instance: 9FF35780A8BC8D2B
Apple-Challenge: 09KF45soMYmvj6dpsUGiIg

v=0
o=iTunes 3172942895 0 IN IP4 10.0.1.101
s=iTunes
c=IN IP4 10.0.1.103
t=0 0
m=audio 0 RTP/AVP 96
a=rtpmap:96 AppleLossless
a=fmtp:96 4096 0 16 40 10 14 2 255 0 0 44100
a=rsaaeskey:5QYIqmdZGTONY5SHjEJrqAhaa0W9wzDC5i6q221mdGZJ5ubO6Kg
      yhC6U83wpY87TFdPRdfPQl2kVC7+Uefmx1bXdIUo07ZcJsqMbgtje4w2JQw0b
      Uw2BlzNPmVGQOxfdpGc3LXZzNE0jI1D4conUEiW6rrzikXBhk7Y/i2naw13ayy
      xaSwtkiJ0ltBQGYGErbV2tx43QSNj7O0JIG9GrF2GZZ6/UHo4VH+ZXgQ4NZvP/
      QXPCsLutZsvusFDzIEq7TN1fveINOiwrzlN+bckEixvhXlvoQTWE2tjbmQYhMvO
      FIly5gNbZiXi0l5AdolX4jDC2vndFHqWDks/3sPikNg
a=aesiv:zcZmAZtqh7uGcEwPXk0QeA
```
</div>
<div class="server_client">
<p>server &rarr; client</p>

```http
RTSP/1.0 200 OK
CSeq: 1
Apple-Response: u+msU8Cc7KBrVPjI/Ir8fOL8+C5D3Jsw1+acaW3MNTndrTQAeb/a
          5m10UVBX6wb/DYQGY+b28ksSwBjN0nFOk4Y2cODEf83FAh7B
          mkLpmpkpplp7zVXQ+Z9DcB6gC60ZsS3t98aoR7tSzVLKZNgi2X2sC+vGsz
          utQxX03HK008VjcdngHv3g1p2knoETd07T6eVfZCmPqp6Ga7Dj8VIIj/GEP3
          AjjDx3lJnQBXUDmxM484YXLXZjWFXCiY8GJt6whjf7/2c3rIoT3Z7PQpEvPmM
          1MXU9cv4NL59Y/q0OAVQ38foOz7eGAhfvjOsCnHU25aik7/7ToIYt1tyVtap/kA
Audio-Jack-Status: connected; type=analog
```
</div>
