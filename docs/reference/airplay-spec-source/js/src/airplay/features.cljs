(ns airplay.features
  (:require [clojure.string :as cs]
            [airplay.math :as am]))

(defn features->mdns [value]
  (let [low (am/bit-and value (am/bits->long 0xffffffff 0))
        high (am/bit-shift-right value 32)]
    (str (am/->hex low) "," (am/->hex high))))

(defn str->features
  "Convert all possible ways to specify features into a long"
  [s]
  (let [[low high] (map cs/trim (cs/split s #"," 2))]
    (if high
      (am/bit-or (am/try-str->long low) (am/bit-shift-left (am/try-str->long high) 32))
      (am/try-str->long low))))
