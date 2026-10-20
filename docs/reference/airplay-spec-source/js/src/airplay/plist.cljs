(ns airplay.plist
  (:require [clojure.string :as s]
            [clojure.data.xml :as xml]
            [airplay.math :as am]
            [goog.crypt.base64 :as base64]
            [clojure.data.xml.node :as node]
            [clojure.data.xml.name :as name]))

(defn decode-base64 [s]
  (base64/decodeStringToByteArray s))
(defn encode-base64 [array]
  (base64/encodeByteArray array))

(defrecord Data [data]
  Object
  (toString [_] (encode-base64 data)))

(defn ->data [data]
  (Data. (decode-base64 data)))

(declare dict->map)

(defn elem->clj [e]
  (let [c (:content e)]
    (case (:tag e)
      :string (s/join "" c)
      :integer (am/str->long (s/join "" c))
      :real (am/parse-float (s/join "" c))
      :true true
      :false false
      :data (->data (s/join "" c))
      :array (map elem->clj c)
      :dict (dict->map c)
      (throw "Invalid element"))))

(defn dict->map [e]
  (->> e
       :content
       (filter xml/element?)
       (partition 2)
       (reduce (fn [val [key value]]
                 (assoc val (s/join "" (:content key)) (elem->clj value)))
               {})))

(defn parse [s]
  (->> s
       xml/parse-str
       :content
       (filter xml/element?)
       first
       dict->map))

(declare xml-el)

(defn- dict-xml
  ([el {:keys [indent indent-level] :as opts}]
   (let [full (apply str (repeat indent-level indent))]
     (mapcat (fn [[key value]]
               [full
                (node/element :key {} (if (keyword? key) (name key) (str key)))
                full
                (xml-el value opts)]) el))))

(defn- pretty-data [data indent]
  (if indent
    (->> (str data)
         (partition 68 68 [])
         (map #(str indent (apply str %) "\n"))
         (apply str))
    (str data)))

(comment
  (pretty-data "test" "\t"))

(defn- xml-el
  ([el] (xml-el el {}))
  ([el {:keys [indent indent-level] :as opts}]
   (let [full (if indent (apply str (repeat indent-level indent)))
         next (assoc opts :indent-level (inc indent-level))
         next-full (if indent (apply str (repeat (inc indent-level) indent)))]
     (cond
       (instance? Data el) (node/element :data {} (pretty-data el full))
       (string? el) (node/element :string {} el)
       (int? el) (node/element :integer {} (str el))
       (double? el) (node/element :real {} (str el))
       (= true el) (node/element :true)
       (= false el) (node/element :false)
       (vector? el) (apply node/element :array {} (mapcat #(concat next-full [(xml-el % next)]) el))
       (satisfies? ILookup el) (apply node/element :dict {} (dict-xml el next))
       (satisfies? ISequential el) (apply node/element :array {} (mapcat #(concat next-full [(xml-el % next)]) el))
       :else (throw (ex-info "Cannot coerce" {:form el}))))))

(defn xml
  ([el] (xml el {}))
  ([el opts]
   (let [next (assoc opts :indent-level 0)]
     (node/element :plist {:version "1.0"} (xml-el el next)))))

(defn emit-str [el]
  (str
    "<?xml version=\\\"1.0\\\" encoding=\\\"UTF-8\\\"?>"
    "<!DOCTYPE plist PUBLIC \\\"-//Apple//DTD PLIST 1.0//EN\\\" \\\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\\\">"
    (xml/emit-str (xml el))))

(declare emit-pretty-xml-str)
(defn- element-str [el {:keys [indent indent-level] :as opts}]
  (let [qname (name (:tag el))
        content (seq (:content el))
        full (apply str (repeat indent-level indent))
        next (assoc opts :indent-level (inc indent-level))
        content-map (if content (map #(emit-pretty-xml-str % next) content))]
    (apply str (concat ["<" qname]
                       (mapcat (fn [[n a]]
                                 [" " (name n) "=" (pr-str a)])
                               (:attrs el))
                       (cond
                         (nil? content) ["/>\n"]
                         (some node/element? content) (concat [">\n"] content-map [full "</" qname ">\n"])
                         :else (if (re-find #"\n" (apply str content-map))
                                 (concat [">\n"] content-map [full "</" qname ">\n"])
                                 (concat [">"] content-map ["</" qname ">\n"])))))))
(defn- emit-pretty-xml-str [el opts]
  (cond
    (string? el) el
    (instance? node/Element el) (element-str el opts)))

(defn emit-pretty-str [el]
  (str
    "<?xml version=\\\"1.0\\\" encoding=\\\"UTF-8\\\"?>\n"
    "<!DOCTYPE plist PUBLIC \\\"-//Apple//DTD PLIST 1.0//EN\\\" \\\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\\\">\n"
    (emit-pretty-xml-str (xml el {:indent "\t"}) {:indent "\t" :indent-level -1})))

(comment
  (name/as-qname (:tag (xml [12 13])))
  (emit-pretty-str (am/long 12))
  (xml [12 13] {:indent "\t"})
  (emit-pretty-str [12 13 {:sdk "test"}])
  (emit-pretty-str {:sourceVersion "360.0"})
  (emit-pretty-str {:test (->data (str "\tBWFjbD0wGmRldmljZWlkPTAwOjA1OkNEOkQ0OjQyOjk2G2ZlYXR1cmVzPTB4NDQ1RjhB\n"
                                       "\tMDAsMHgxQzM0MAdyc2Y9MHgwEGZ2PXAyMC4xLjUwNS4xMzAJZmxhZ3M9MHg0EG1vZGVs\n"
                                       "\tPUFWUi1YMzUwMEgZbWFudWZhY3R1cmVyPVNvdW5kIFVuaXRlZBtzZXJpYWxOdW1iZXI9\n"
                                       "\tQkJXMzYxODEyMTIzNjQNcHJvdG92ZXJzPTEuMQ1zcmN2ZXJzPTM2Ni4wJ3BpPTY1MGMy\n"
                                       "\tMGVlLTg0MmMtNGMwMS04MGVkLTIyYjdmYjIzOTg4MShnaWQ9NjUwYzIwZWUtODQyYy00\n"
                                       "\tYzAxLTgwZWQtMjJiN2ZiMjM5ODgxBmdjZ2w9MENwaz0wYWNkN2Q2MWIyODRjMGFmNzFj\n"
                                       "\tN2VmNGY3ZWE2NDRkZGRlYzIzOGVmMjdjM2MwYWQzZDVkM2JiOWM4YjMxZThm"))})
  (re-find #"\n" "test")
  (re-find #"\n"
           (str "\tBWFjbD0wGmRldmljZWlkPTAwOjA1OkNEOkQ0OjQyOjk2G2ZlYXR1cmVzPTB4NDQ1RjhB\n"
                "\tMDAsMHgxQzM0MAdyc2Y9MHgwEGZ2PXAyMC4xLjUwNS4xMzAJZmxhZ3M9MHg0EG1vZGVs\n"
                "\tPUFWUi1YMzUwMEgZbWFudWZhY3R1cmVyPVNvdW5kIFVuaXRlZBtzZXJpYWxOdW1iZXI9\n"
                "\tQkJXMzYxODEyMTIzNjQNcHJvdG92ZXJzPTEuMQ1zcmN2ZXJzPTM2Ni4wJ3BpPTY1MGMy\n"
                "\tMGVlLTg0MmMtNGMwMS04MGVkLTIyYjdmYjIzOTg4MShnaWQ9NjUwYzIwZWUtODQyYy00\n"
                "\tYzAxLTgwZWQtMjJiN2ZiMjM5ODgxBmdjZ2w9MENwaz0wYWNkN2Q2MWIyODRjMGFmNzFj\n"
                "\tN2VmNGY3ZWE2NDRkZGRlYzIzOGVmMjdjM2MwYWQzZDVkM2JiOWM4YjMxZThm"))
  )