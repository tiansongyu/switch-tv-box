(ns airplay.ui.flags
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


(defonce status-flags
  (if-let [table (ab/first-table (sel1 "#status-flags"))]
    (if (not (dc/closest table :#status-flags-list))
      (ab/bits-table->map table))))


(defn status-flags-table [{:keys [value on-input-changed on-change-bit status-flags]}]
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
        "Hex: " (am/->hex l-value)]]]
     ^{:key "head"}
     [:tbody {:className ["header"]}
      [:tr
       ^{:key "bit"} [:th "bit"]
       (for [name (->> status-flags vals first keys)]
         ^{:key name} [:th name])]]
     ^{:key "body"}
     [:tbody {:className "rows"}
      (for [[bit values] status-flags]
        ^{:key bit}
        [:tr {:class    (if (am/bit-test l-value bit) "selected" "notSelected")
              :on-click #(on-change-bit bit)}
         ^{:key "bit"} [:td bit]
         (for [[name value] values]
           ^{:key name} [:td value])])]]))

(as/reg-event-fx ::change-value
                 (fn [{:keys [db]} [_ new-value]]
                   {:db       (assoc-in db [:current :flags] new-value)
                    :dispatch [:airplay.ui.core/current-changed]}))
(as/reg-event-fx ::flip-bit
                 (fn [{:keys [db]} [_ bit]]
                   {:db       (update-in db [:current :flags] #(-> % am/try-str->long (am/bit-flip bit) am/->hex))
                    :dispatch [:airplay.ui.core/current-changed]}))
(as/reg-sub ::value
            (fn [db _]
              (get-in db [:current :flags])))

(defn status-flags-state []
  (js/console.log "status-flags-state")
  (let [#_ctx #_(.-context (rc/current-component))
        value (as/subscribe #_ctx [::value])]
    (js/console.log "Component" (rc/current-component) value)
    [status-flags-table {:status-flags           status-flags
                     :value              value
                     :on-input-changed   (fn [i]
                                           (let [new-value (-> i .-target .-value)]
                                             (js/console.log "Change value" new-value)
                                             (as/dispatch #_ctx [::change-value new-value])))
                     :on-change-bit      #(as/dispatch #_ctx [::flip-bit %])}]))

(defn status-flags-ratom []
  (let [value (rc/atom "1")]
    [status-flags-table {:status-flags           status-flags
                     :value              value
                     :on-input-changed   (fn [i]
                                           (let [new-value (-> i .-target .-value)]
                                             (js/console.log "Change value" new-value)
                                             (reset! value new-value)))
                     :on-change-bit      (fn [bit] (swap! value #(-> % am/try-str->long (am/bit-flip bit) am/->hex)))}]))


(defn init []
  (if-let [el (sel1 "#status-flags")]
    (let [main (-> el dc/parent)
          container (or (sel1 main "#status-flags-list")
                        (let [elem (dc/create-element "div")]
                          (dc/set-attr! elem "id" "status-flags-list")
                          (dc/replace! (ab/first-table el) elem)))]
      (rc/render #_auc/app status-flags-state container))))
