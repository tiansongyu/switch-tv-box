(ns airplay.examples.data
  (:require [clojure.string :as s]))

(defn deep-merge* [& maps]
  (let [f (fn [old new]
            (if (and (map? old) (map? new))
              (merge-with deep-merge* old new)
              new))]
    (if (every? map? maps)
      (apply merge-with f maps)
      (last maps))))

(defn deep-merge [& maps]
  (let [maps (filter identity maps)]
    (assert (every? map? maps))
    (apply merge-with deep-merge* maps)))

(defn parse-txt [txt]
  (cond
    (string? txt) (parse-txt (s/split-lines txt))
    (map? txt) txt
    (and (sequential? txt) (string? (first txt))) (->> txt
                                                       (map #(s/split % #"=" 2))
                                                       (map (fn [[k v]] [(keyword k) v]))
                                                       (into {}))))


(comment
  (parse-txt "cn=0,1,2,3\nda=true\net=0,3,5\nft=0x4A7FFFF7,0x3C155FDE\nsf=0x10644\nmd=0,1,2\nam=AppleTV6,2\npk=23a4f82385f4de8f68888908d72d6cb5d229e0ee50fb9359b6b2807e68f4aa23\ntp=UDP\nvn=65537\nvs=415.3\nov=13.3\nvv=2")
  (parse-txt ["cn=0,1,2,3"
              "da=true"
              "et=0,3,5"
              "ft=0x4A7FFFF7,0x3C155FDE"
              "sf=0x10644"
              "md=0,1,2"
              "am=AppleTV6,2"
              "pk=23a4f82385f4de8f68888908d72d6cb5d229e0ee50fb9359b6b2807e68f4aa23"
              "tp=UDP"
              "vn=65537"
              "vs=415.3"
              "ov=13.3"
              "vv=2"])
  )

(def apple-tv
  (let [base {:name     "4K Apple TV"
              :features "0x4A7FFFF7,0x4155FDE"
              :services {
                         "_airplay._tcp" {:name "Stue"
                                          :port 7000
                                          :txt  {:acl       "0"
                                                 :btaddr    "90:DD:5D:97:66:81"
                                                 :deviceid  "90:DD:5D:98:F0:0A"
                                                 :features  "0x4A7FFFF7,0x3C155FDE"
                                                 :flags     "0x10644"
                                                 :gid       "2F5E2C72-B31B-43E1-A4A5-AD6C2A3926A6"
                                                 :igl       "1"
                                                 :gcgl      "1"
                                                 :model     "AppleTV6,2"
                                                 :protovers "1.1"
                                                 :pi        "de159742-c022-4514-915b-203cb99f8b71"
                                                 :psi       "24609858-187F-424E-814B-A45B0C756B22"
                                                 :pk        "23a4f82385f4de8f68888908d72d6cb5d229e0ee50fb9359b6b2807e68f4aa23"
                                                 :srcvers   "415.3"
                                                 :osvers    "13.3"
                                                 :vv        "2"}}
                         "_raop._tcp"    {:name "90DD5D98F00A@Stue"
                                          :port 7000
                                          :txt  (parse-txt ["cn=0,1,2,3"
                                                            "da=true"
                                                            "et=0,3,5"
                                                            "ft=0x4A7FFFF7,0x3C155FDE"
                                                            "sf=0x10644"
                                                            "md=0,1,2"
                                                            "am=AppleTV6,2"
                                                            "pk=23a4f82385f4de8f68888908d72d6cb5d229e0ee50fb9359b6b2807e68f4aa23"
                                                            "tp=UDP"
                                                            "vn=65537"
                                                            "vs=415.3"
                                                            "ov=13.3"
                                                            "vv=2"])}}
              :info     {:xml    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n<plist version=\"1.0\">\n<dict>\n\t<key>deviceID</key>\n\t<string>90:DD:5D:98:F0:0A</string>\n\t<key>features</key>\n\t<integer>294246758999916535</integer>\n\t<key>initialVolume</key>\n\t<real>-20</real>\n\t<key>keepAliveSendStatsAsBody</key>\n\t<true/>\n\t<key>macAddress</key>\n\t<string>90:DD:5D:A2:D0:20</string>\n\t<key>model</key>\n\t<string>AppleTV6,2</string>\n\t<key>name</key>\n\t<string>Stue</string>\n\t<key>pi</key>\n\t<string>de159742-c022-4514-915b-203cb99f8b71</string>\n\t<key>pk</key>\n\t<data>\n\tI6T4I4X03o9oiIkI1y1stdIp4O5Q+5NZtrKAfmj0qiM=\n\t</data>\n\t<key>playbackCapabilities</key>\n\t<dict>\n\t\t<key>supportsFPSSecureStop</key>\n\t\t<true/>\n\t\t<key>supportsUIForAudioOnlyContent</key>\n\t\t<true/>\n\t</dict>\n\t<key>protocolVersion</key>\n\t<string>1.1</string>\n\t<key>psi</key>\n\t<string>24609858-187F-424E-814B-A45B0C756B22</string>\n\t<key>senderAddress</key>\n\t<string>10.42.0.17:55587</string>\n\t<key>sourceVersion</key>\n\t<string>380.20.1</string>\n\t<key>statusFlags</key>\n\t<integer>67140</integer>\n\t<key>txtAirPlay</key>\n\t<data>\n\tBWFjbD0wGmRldmljZWlkPTkwOkREOjVEOjk4OkYwOjBBHWZlYXR1cmVzPTB4NEE3RkZG\n\tRjcsMHg0MTU1RkRFDWZsYWdzPTB4MTA2NDQoZ2lkPTBFQjU1RkRGLTQwOTAtNEY4OS04\n\tMEU0LUJGRTZBQkIzOEJBQgVpZ2w9MQZnY2dsPTEQbW9kZWw9QXBwbGVUVjYsMg1wcm90\n\tb3ZlcnM9MS4xJ3BpPWRlMTU5NzQyLWMwMjItNDUxNC05MTViLTIwM2NiOTlmOGI3MShw\n\tc2k9MjQ2MDk4NTgtMTg3Ri00MjRFLTgxNEItQTQ1QjBDNzU2QjIyQ3BrPTIzYTRmODIz\n\tODVmNGRlOGY2ODg4ODkwOGQ3MmQ2Y2I1ZDIyOWUwZWU1MGZiOTM1OWI2YjI4MDdlNjhm\n\tNGFhMjMQc3JjdmVycz0zODAuMjAuMQ1vc3ZlcnM9MTIuMi4xBHZ2PTI=\n\t</data>\n\t<key>volumeControlType</key>\n\t<integer>4</integer>\n\t<key>vv</key>\n\t<integer>2</integer>\n</dict>\n</plist>\n"
                         :base64 "YnBsaXN0MDDfEBIBAgMEBQYHCAkKCwwNDg8QERITFBkaGxwdHh8gISIjFyUm\nJyhadHh0QWlyUGxheV8QFHBsYXliYWNrQ2FwYWJpbGl0aWVzWm1hY0FkZHJl\nc3Ndc2VuZGVyQWRkcmVzc1JwaVJ2dltzdGF0dXNGbGFnc18QEXZvbHVtZUNv\nbnRyb2xUeXBlXXNvdXJjZVZlcnNpb25TcHNpXWluaXRpYWxWb2x1bWVfEA9w\ncm90b2NvbFZlcnNpb25ScGtfEBhrZWVwQWxpdmVTZW5kU3RhdHNBc0JvZHlY\nZGV2aWNlSURVbW9kZWxYZmVhdHVyZXNUbmFtZU8RAVsFYWNsPTAaZGV2aWNl\naWQ9OTA6REQ6NUQ6OTg6RjA6MEEdZmVhdHVyZXM9MHg0QTdGRkZGNywweDQx\nNTVGREUNZmxhZ3M9MHgxMDY0NChnaWQ9MEVCNTVGREYtNDA5MC00Rjg5LTgw\nRTQtQkZFNkFCQjM4QkFCBWlnbD0xBmdjZ2w9MRBtb2RlbD1BcHBsZVRWNiwy\nDXByb3RvdmVycz0xLjEncGk9ZGUxNTk3NDItYzAyMi00NTE0LTkxNWItMjAz\nY2I5OWY4YjcxKHBzaT0yNDYwOTg1OC0xODdGLTQyNEUtODE0Qi1BNDVCMEM3\nNTZCMjJDcGs9MjNhNGY4MjM4NWY0ZGU4ZjY4ODg4OTA4ZDcyZDZjYjVkMjI5\nZTBlZTUwZmI5MzU5YjZiMjgwN2U2OGY0YWEyMxBzcmN2ZXJzPTM4MC4yMC4x\nDW9zdmVycz0xMi4yLjEEdnY9MtIVFhcXXxAVc3VwcG9ydHNGUFNTZWN1cmVT\ndG9wXxAdc3VwcG9ydHNVSUZvckF1ZGlvT25seUNvbnRlbnQJCV8QETkwOkRE\nOjVEOkEyOkQwOjIwXxAQMTAuNDIuMC4xNzo1NTU4N18QJGRlMTU5NzQyLWMw\nMjItNDUxNC05MTViLTIwM2NiOTlmOGI3MRACEgABBkQQBFgzODAuMjAuMV8Q\nJDI0NjA5ODU4LTE4N0YtNDI0RS04MTRCLUE0NUIwQzc1NkIyMiPANAAAAAAA\nAFMxLjFPECAjpPgjhfTej2iIiQjXLWy10ing7lD7k1m2soB+aPSqIwlfEBE5\nMDpERDo1RDo5ODpGMDowQVpBcHBsZVRWNiwyEwQVX95Kf//3VFN0dWUACAAv\nADoAUQBcAGoAbQBwAHwAkACeAKIAsADCAMUA4ADpAO8A+AD9AlwCYQJ5ApkC\nmgKbAq8CwgLpAusC8ALyAvsDIgMrAy8DUgNTA2cDcgN7AAAAAAAAAgEAAAAA\nAAAAKQAAAAAAAAAAAAAAAAAAA4A=\n"}}
        ]
    (into (sorted-map)
          {:apple-tv-not-playing (deep-merge base {:name     "4K Apple TV Not playing"
                                                   :flags    "0x10644"
                                                   :services {"_airplay._tcp" {:txt {:flags "0x10644"
                                                                                     :gid   "712F0759-5D44-41E7-AB67-FAB0AD39E165"
                                                                                     :igl   "1"
                                                                                     :gcgl  "1"}}}})
           :apple-tv-audio       (deep-merge base {:name     "4K Apple TV receiving AirPlay audio"
                                                   :flags    "0x30e44"
                                                   :services {"_airplay._tcp" {:txt {:flags "0x30e44"
                                                                                     :gid   "19F5D4B2-8A06-4792-923E-8AFA83913238"
                                                                                     :igl   "0"
                                                                                     :gcgl  "0"
                                                                                     :pgid  "19F5D4B2-8A06-4792-923E-8AFA83913238"
                                                                                     :pgcgl "0"}}}})
           :apple-tv-video       (deep-merge base {:name     "4K Apple TV receiving AirPlay video"
                                                   :flags    "0x30e44"
                                                   :services {"_airplay._tcp" {:txt {:flags "0x30e44"
                                                                                     :gid   "19F5D4B2-8A06-4792-923E-8AFA83913238"
                                                                                     :igl   "0"
                                                                                     :gcgl  "0"
                                                                                     :pgid  "19F5D4B2-8A06-4792-923E-8AFA83913238"
                                                                                     :pgcgl "0"}}}})
           :apple-tv-screen      (deep-merge base {:name     "4K Apple TV receiving AirPlay screen"
                                                   :flags    "0x30644"
                                                   :services {"_airplay._tcp" {:txt {:flags "0x30644"
                                                                                     :gid   "712F0759-5D44-41E7-AB67-FAB0AD39E165"
                                                                                     :igl   "1"
                                                                                     :gcgl  "1"}}}})
           :apple-tv-local       (deep-merge base {:name     "4K Apple TV playing local media"
                                                   :flags    "0x10644"
                                                   :services {"_airplay._tcp" {:txt {:flags "0x10644"
                                                                                     :gid   "FA69F5A2-5574-4E4D-A676-CA7F61A904A3"
                                                                                     :igl   "1"
                                                                                     :gcgl  "1"}}}})})))

(def all
  (merge apple-tv
         {:homepod-slave            {:name     "HomePod (Stereo Pair Slave)"
                                     :features "0x4A7FCA00,0x4156BD0"
                                     :flags    "0x3ac04"}
          :homepod-group-leader     {:name     "HomePod (Stereo Pair Leader)"
                                     :features "0x4A7FCA00,0x4156BD0"
                                     :flags    "0x18404"}
          :airport-express          {:name     "Airport Express"
                                     :features "0x445D0A00,0x1C340"
                                     :flags    "0x4"}
          :denon-avr-x3500h         {:name     "Denon AVR-X3500H"
                                     :features "0x445F8A00,0x1C340"
                                     :flags    "0x4"}
          :denon-avr-x3500h-playing {:name     "Denon AVR-X3500H receiving AirPlay audio"
                                     :features "0x445F8A00,0x1C340"
                                     :flags    "0x804"}
          :denon-avr-x1400h         {:name     "Denon AVR-X1400H"
                                     :features "0x445F8A00,0x1C340"
                                     :flags    "0x4"}
          :denon-avr-x4400h         {:name     "Denon AVR-X4400H"
                                     :features "0x445F8A00,0x1C340"
                                     :flags    "0x4"}
          :libretone-lth200         {:name     "Libretone LTH200"
                                     :features "0x445C0A00,0x1C340"
                                     :flags    "0x484"}
          :sono-beam                {:name     "Sonos Beam"
                                     :features "0x445F8A00,0x1C340"
                                     :flags    "0x404"}
          :sonos-one                {:name     "Sonos One"
                                     :features "0x445F8A00,0x1C340"
                                     :flags    "0x404"}
          :airserver                {:name     "AirServer"
                                     :features "0x4A7FFFF7,0xE"
                                     :flags    "0x4"}
          :airplay                  {:name     "Airplay"
                                     :features "0x5A7FFFF7,0x1E"
                                     :flags    "0x4"}
          :multi-room               {:name     "Required for multi-room audio"
                                     :features "0x40180200,0x300"
                                     :flags    "0x4"}})
  )
