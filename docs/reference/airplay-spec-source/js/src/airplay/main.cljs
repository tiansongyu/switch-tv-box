(ns airplay.main
  (:require [clojure.string :as cs]
            [dommy.core :as dc :refer-macros [sel sel1]]
            [reagent.core :as rc]
            [react :as react]
            [re-frame.core :as rf]
            [airplay.math :as am]
            [airplay.plist :as plist]
            [airplay.ui.features :as auf]
            [airplay.ui.examples :as aue]
            [airplay.ui.flags :as aus]
            [airplay.ui.airplay-service :as auas]
            [clojure.data.xml :as xml]
            [clojure.string :as s]
            [goog.crypt.base64 :as base64]))

#_(reset! thonix/app (core/mount @thonix/app app/Root "app"))

(js/console.log "UI")


(comment
  (-> (sel1 "#info code")
      dc/text)
  (-> (sel1 "#info code")
      dc/text
      plist/parse)
  (let [txt (-> (sel1 "#info code")
                 dc/text)]
    (= (-> txt
           plist/parse
           plist/emit-pretty-str)
       txt))
  (first (filter xml/element? (:content (xml/parse-str (dc/text (sel1 "#info code"))))))
  (bit-set)
  (dc/closest) (map dc/text (sel [:table "td:first-child"])) :#testing
  (dc/selector [:thead :th])
  (->> (sel (sel1 :table) [:tbody :tr])
       (map #(sel1 %1 :td))
       (map dc/text)
       )
  (map #(sel1 %1 :td) (sel (sel1 :table) [:tbody :tr]))
  (map dc/text (sel (sel1 :table) [:thead :th]))
  (let [table (sel1 :table)]
    (if (not (dc/closest table :#features-list))
      (bits-table->map table)))
  (bits-table->map (sel1 :table))
  features
  (or (sel1 :#features-list) (sel1 :table)))

(defn ^:export main []
  (js/console.log "Main")
  (rf/dispatch-sync [:initialize])
  (auf/init)
  (aue/init)
  (aus/init)
  #_(auas/init))
(main)

(defn reload []
  (rf/clear-subscription-cache!)
  #_(rc/force-update-all))
