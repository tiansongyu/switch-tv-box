(ns airplay.ui.features
  (:require [clojure.string :as cs]
            [dommy.core :as dc :refer-macros [sel sel1]]
            [dommy.utils :as du]
            [reagent.core :as rc]
            [react :as react]
            [airplay.features :as af]
            [airplay.math :as am]
            [airplay.plist :as plist]
            [airplay.bits :as ab]
            [re-frame.core :as as]
            [clojure.data.xml :as xml]
            [clojure.string :as s]))


(defonce features
  (if-let [table (ab/first-table (sel1 "#features"))]
    (if (not (dc/closest table :#features-list))
      (ab/bits-table->map table))))


(defn features-table [{:keys [value on-input-changed on-change-bit features]}]
  (let [l-value (af/str->features (or @value ""))]
    [:table {:className "bits"}
     ^{:key "interactive"}
     [:tbody {:className ["header"]}
      [:tr
       [:th {:colSpan 3}
        "Example: "
        [:input {:type      "text"
                 :on-change on-input-changed
                 :on-paste  on-input-changed
                 :value     @value}]]]]
     ^{:key "output"}
     [:tbody
      [:tr
       [:td {:colSpan 3}
        "Decimal: " (am/->str l-value)]]
      [:tr
       [:td {:colSpan 3}
        "Hex: " (am/->hex l-value)]]
      [:tr
       [:td {:colSpan 3}
        "mDNS: " (af/features->mdns l-value)]]]
     ^{:key "head"}
     [:tbody {:className ["header"]}
      [:tr
       ^{:key "bit"} [:th "bit"]
       (for [name (->> features vals first keys)]
         ^{:key name} [:th name])]]
     ^{:key "body"}
     [:tbody {:className "rows"}
      (for [[bit values] features]
        ^{:key bit}
        [:tr {:class    (if (am/bit-test l-value bit) "selected" "notSelected")
              :on-click #(on-change-bit bit)}
         ^{:key "bit"} [:td bit]
         (for [[name value] values]
           ^{:key name} [:td value])])]]))

(as/reg-event-fx ::change-value
                 (fn [{:keys [db]} [_ new-value]]
                   {:db       (assoc-in db [:current :features] new-value)
                    :dispatch [:airplay.ui.core/current-changed]}))
(as/reg-event-fx ::flip-bit
                 (fn [{:keys [db]} [_ bit]]
                   {:db       (update-in db [:current :features] #(-> % af/str->features (am/bit-flip bit) af/features->mdns))
                    :dispatch [:airplay.ui.core/current-changed]}))
(as/reg-sub ::value
            (fn [db _]
              (get-in db [:current :features])))

(defn features-state []
    (js/console.log "features-state")
    (let [#_ctx #_(.-context (rc/current-component))
                  value (as/subscribe #_ctx [::value])]
      (js/console.log "Component" (rc/current-component) value)
      [features-table {:features           features
                       :value              value
                       :on-input-changed   (fn [i]
                                             (let [new-value (-> i .-target .-value)]
                                               (js/console.log "Change value" new-value)
                                               (as/dispatch #_ctx [::change-value new-value])))
                       :on-change-bit      #(as/dispatch #_ctx [::flip-bit %])}]))

(defn features-ratom []
  (let [value (rc/atom "1")]
    [features-table {:features           features
                     :value              value
                     :on-input-changed   (fn [i]
                                           (let [new-value (-> i .-target .-value)]
                                             (js/console.log "Change value" new-value)
                                             (reset! value new-value)))
                     :on-change-bit      (fn [bit] (swap! value #(-> % af/str->features (am/bit-flip bit) af/features->mdns)))}]))


(defn init []
  (if-let [el (sel1 "#features")]
    (let [main (-> el dc/parent)
          container (or (sel1 main "#features-list")
                        (let [elem (dc/create-element "div")]
                          (dc/set-attr! elem "id" "features-list")
                          (dc/replace! (ab/first-table el) elem)))]
      (rc/render #_auc/app features-state container))))


(comment
  (let [el (sel1 "#features")]
    (let [main (-> el dc/parent)
          container (or (sel1 main "#features-list")
                        (let [elem (dc/create-element "div")]
                          (dc/set-attr! elem "id" "features-list")
                          (dc/replace! (ab/first-table el) elem)))]
      container))
  (main)
  (ab/first-table (sel1 "#features"))
  (map cs/trim (cs/split "0x4A7FFFF7,0x4155FDE" #"," 2))
  (am/bit-test (str->features "0x4A7FFFF0,0x4155FDE") 0)
  (am/->hex (am/try-str->long "0x4A7FFFF7"))
  (am/->hex (am/bit-shift-left (am/try-str->long "0x4A7FFFF7") 24))
  (Long )
  (let [[low high] (map cs/trim (cs/split "0x4A7FFFF7,0x4155FDE" #"," 2))]
    (if high
      (am/bit-or (am/try-str->long low) (am/bit-shift-left (am/try-str->long high) 32))
      (am/try-str->long low)))
  (am/->hex (str->features "0x4A7FFFF7,0x4155FDE"))
  (-> (sel1 "#features")
      dc/parent
      dc/parent
      (sel1 :table)))

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