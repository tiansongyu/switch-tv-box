(ns airplay.ui.airplay-service
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


(defonce airplay-items
  (if-let [table (ab/first-table (sel1 "#_airplay_tcp"))]
    (if (not (dc/closest table :#airplay-list))
      (ab/table->map table))))

(comment
   airplay-items)

(defn airplay-table [{:keys [value on-input-changed on-change-bit items]}]
  (let [l-value (or @value {})]
    [:table {:className "bits"}
     ^{:key "head"}
     [:tbody {:className ["header"]}
      [:tr
       ^{:key "name"} [:th "name"]
       ^{:key "value"} [:th "value"]
       (for [name (->> items vals first keys)]
         ^{:key name} [:th name])]]
     ^{:key "body"}
     [:tbody {:className "rows"}
      (for [[key values] items]
        ^{:key (name key)}
        [:tr
         ^{:key "name"} [:td (name key)]
         ^{:key "value"} [:td (get l-value key "")]
         (for [[key value] values]
           ^{:key (name key)} [:td value])])]]))

(as/reg-event-fx ::change-value
                 (fn [{:keys [db]} [_ new-value]]
                   {:db       (assoc-in db [:current :services "_airplay._tcp" :txt] new-value)
                    :dispatch [:airplay.ui.core/current-changed]}))
(as/reg-event-fx ::flip-bit
                 (fn [{:keys [db]} [_ bit]]
                   {:db       (update-in db [:current :services "_airplay._tcp" :txt]
                                         #(-> % am/try-str->long (am/bit-flip bit) am/->hex))
                    :dispatch [:airplay.ui.core/current-changed]}))
(as/reg-sub ::value
            (fn [db _]
              (get-in db [:current :services "_airplay._tcp" :txt])))

(defn airplay-state []
  (js/console.log "airplay-state")
  (let [#_ctx #_(.-context (rc/current-component))
        value (as/subscribe #_ctx [::value])]
    (js/console.log "Component" (rc/current-component) value)
    [airplay-table {:items            airplay-items
                    :value            value
                    :on-input-changed (fn [i]
                                        (let [new-value (-> i .-target .-value)]
                                          (js/console.log "Change value" new-value)
                                          (as/dispatch #_ctx [::change-value new-value])))
                    :on-change-bit    #(as/dispatch #_ctx [::flip-bit %])}]))

(defn airplay-ratom []
  (let [value (rc/atom "1")]
    [airplay-table {:status-flags           airplay-items
                         :value              value
                         :on-input-changed   (fn [i]
                                               (let [new-value (-> i .-target .-value)]
                                                 (js/console.log "Change value" new-value)
                                                 (reset! value new-value)))
                         :on-change-bit      (fn [bit] (swap! value #(-> % am/try-str->long (am/bit-flip bit) am/->hex)))}]))


(defn init []
  (if-let [el (sel1 "#_airplay_tcp")]
    (let [main (-> el dc/parent)
          container (or (sel1 main "#airplay-list")
                        (let [elem (dc/create-element "div")]
                          (dc/set-attr! elem "id" "airplay-list")
                          (dc/replace! (ab/first-table el) elem)))]
      (rc/render #_auc/app airplay-state container))))
