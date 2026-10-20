(ns airplay.math
  (:refer-clojure :exclude [long bit-and bit-or bit-xor bit-not bit-shift-left bit-shift-right bit-test bit-set bit-flip])
  (:import goog.math.Long))

(extend-type Long
  IEquiv
  (-equiv [o other] (.equals o other)))

(defn parse-int
  ([s] (js/parseInt s))
  ([s radix] (js/parseInt s radix)))

(defn parse-float [s]
  (js/parseFloat s))

(defn long [x]
  (if (instance? Long x)
    x
    (condp = x
      0 (.getZero Long)
      1 (.getOne Long)
      -1 (.getNegOne Long)
      (.fromNumber Long x))))

(defn ->str
  "Convert long to string"
  ([v] (->str v 10))
  ([v radix] (.toString (long v) radix)))

(defn ->hex [value]
  (str "0x" (.toUpperCase (->str value 16))))

(defn bit-and
  "Bitwise and"
  ([x y] (.and (long x) (long y)))
  ([x y & more] (reduce bit-and (bit-and x y) more)))

(defn bit-or
  "Bitwise or"
  ([x y] (.or (long x) (long y)))
  ([x y & more] (reduce bit-or (bit-or x y) more)))

(defn bit-xor
  "Bitwise exclusive or"
  ([x y] (.xor (long x) (long y)))
  ([x y & more] (reduce bit-xor (bit-xor x y) more)))

(defn bit-not
  "Bitwise complement"
  ([x y] (.not (long x) (long y)))
  ([x y & more] (reduce bit-not (bit-not x y) more)))

(defn bit-shift-left
  "Bitwise shift left"
  ([x n] (.shiftLeft (long x) n)))

(defn bit-shift-right
  "Bitwise shift right"
  [x n] (.shiftRight (long x) n))

(defn bit-test
  "Test bit at index n"
  [x n]
  (let [bitv (bit-shift-left 1 n)]
    (= bitv (bit-and x bitv))))

(defn bit-set
  "Set bit at index n"
  [x n]
  (bit-or x (bit-shift-left 1 n)))

(defn bit-flip
  "Flips bit at index n"
  [x n]
  (bit-xor x (bit-shift-left 1 n)))

(defn str->long
  "Convert string to long with radix support"
  ([s] (str->long s 10))
  ([s radix]
   (.fromString Long s radix)))

(defn try-str->long
  "Try parsing a string and return 0 if not parsable"
  [s]
  (try
    (if (= "0x" (subs s 0 2))
      (str->long (subs s 2) 16)
      (str->long s))
    (catch :default e
      (long 0))))

(defn bits->long
  "Convert low and high bits to long"
  [low high] (.fromBits Long low high))

(comment
  (= (long 12) 10)
  (bit-test 8 3)
  (bit-and 12 12)
  (bit-and )
  (bit-or 10 (.getMaxValue Long))
  (str->long "14")
  (->hex) (.shiftLeft (try-str->long "0x4A7FFFF7") 32)
  (.toString (long (.fromBits Long 0 0x4A7FFFF7)) 16)
  (->hex (bit-shift-left (try-str->long "0x1") 32)))