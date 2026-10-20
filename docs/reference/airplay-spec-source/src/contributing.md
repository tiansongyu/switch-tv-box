# Contributing

## Device Data

If anybody wants to help out who has some Airplay compatible hardware we
can always use some more data. Primarily we right now need mDNS records
and the results from RTSP <code>GET /info/</code> requests. If you have
a computer other than a windows machine I have written some short guides
below and if that is not enough I can probably help you if you send
an e-mail to <a href="brian@maven-group.org">brian@maven-group.org</a>.

We right now have data for a couple of 4K Apple TVs, an AirPort Express,
two stereo paired HomePods, the Libretone LTH200 and a Denon AVR-X3500X
surround receiver so if your hardware is something else I am especially
interested in the data from it.

Any data that you send will only be viewed by me and I will change stuff
like device names, ip addresses, MAC addresses, serial numbers and UUIDs
before including the data in this spec.

### Linux

If you are on Linux and have <code>avahi</code> installed you can run the following
shell script to gather all the data.

```bash
#!/bin/bash
avahi-browse -prt _airplay._tcp | awk -v "FS=;" '{ print $7 }' | sed '/^\s*$/d' > devices.txt
avahi-browse -aprt | grep -f devices.txt > mDNS.txt
for host in $(cat devices.txt) ; do
PORT="$(avahi-browse -prt _airplay._tcp | grep ";$host;" | awk -v "FS=;" '{print $9}' | sed '/^\s*$/d')"
curl https://openairplay.github.io/airplay-spec/data/RTSP-get-info-req.bin | nc -w 2 "$host" $PORT > "RTSP-get-info-res-$host.bin"
done
tar czvf device-data.tar.gz mDNS.txt RTSP-get-info-res*
```

Then you can send the resulting file `device-data.tar.gz`
as an e-mail to [brian@maven-group.org](mailto:brian@maven-group.org)


### macOS

To find mDNS records on macOS I usually use the free
[Discovery](https://itunes.apple.com/dk/app/discovery-dns-sd-browser/id1381004916?mt=12).
From there you can find all the devices that publish a `_airplay._tcp`
service, select and copy/paste the results into an e-mail. It is also
relevant if you can find what other services those devices publish and
include the service records for those services.

To get the RTSP <code>GET /info</code> results you will need the ip address and
port number your devices use for `_airplay._tcp` and in the command below
replace `[ip address]`, `[port]` and `[device name]` with the correct values.


```bash
curl https://openairplay.github.io/airplay-spec/data/RTSP-get-info-req.bin | nc -w 2 [ip address] [port] > RTSP-get-info-res-[device name].bin
```


Then you can send the mDNS records and `RTSP-get-info-res-[device name].bin`
files in an e-mail to [brian@maven-group.org](mailto:brian@maven-group.org)
