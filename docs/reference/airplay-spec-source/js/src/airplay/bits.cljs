(ns airplay.bits
  (:require [dommy.core :as dc :refer-macros [sel sel1]]
            [airplay.math :as am]
            [dommy.utils :as du]))

(defn table->map [table]
  (let [names (rest (map dc/text (sel table [:thead :th])))
        bits (->> (sel table [:tbody "td:first-child"])
                  (map dc/text)
                  (map keyword))]
    (->> (sel table [:tbody :tr])
         (map #(->> (sel %1 :td)
                    rest
                    (map dc/text)
                    (zipmap names)))
         (zipmap bits)
         (into (sorted-map)))))

(defn bits-table->map [table]
  (let [names (rest (map dc/text (sel table [:thead :th])))
        bits (->> (sel table [:tbody "td:first-child"])
                  (map dc/text)
                  (map am/parse-int))]
    (->> (sel table [:tbody :tr])
         (map #(->> (sel %1 :td)
                    rest
                    (map dc/text)
                    (zipmap names)))
         (zipmap bits)
         (into (sorted-map)))))

(defn find-table* [el]
  (if (= "TABLE" (.-tagName el))
    el
    (sel1 el :table)))

(defn first-table [header]
  (if-let [f header]
    (let [h (dc/parent f)]
      (->> h
           dc/parent
           dc/children
           du/->Array
           (drop-while #(not (= % h)))
           (map find-table*)
           (filter some?)
           first))))

(comment
  (if-let [f (sel1 "#features")]
    (let [h (dc/parent f)]
      (->> h
           dc/parent
           dc/children
           du/->Array
           (drop-while #(not (= % h)))
           (map find-table*)
           (filter some?)
           first)))
  (first-table (sel1 "#features")))