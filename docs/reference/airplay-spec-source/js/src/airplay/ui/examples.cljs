(ns airplay.ui.examples
  (:require [re-frame.core :as as]
            [airplay.examples.data :as aed]
            [reagent.core :as rc]
            [dommy.core :as dc]
            [airplay.bits :as ab]
            [airplay.ui.core :as auc]))



(defn examples-select [{:keys [value examples on-example-changed]}]
  (let [selected @value]
    (js/console.log "Render example" selected (if selected (name selected) ""))
    [:<> "Examples: "
     [:select {:on-change on-example-changed :value (if selected (name selected) "")}
      [:option {:key "" :value ""} "Custom"]
      (for [[id example] examples]
        [:option {:key (name id) :value (name id)} (:name example)])]]))

(as/reg-event-fx ::change-value
                 (fn [{:keys [db]} [_ new-value]]
                   (let [example (if-not (= "" new-value) (keyword new-value))
                         selected (get aed/all example)]
                     (js/console.log "Change example" example selected)
                     {:db (assoc db
                            :example example
                            :current selected)})))

(as/reg-sub ::value
            (fn [db _]
              (get-in db [:example])))

(defn examples-select-state []
  (rc/with-let [#_ctx #_(.-context (rc/current-component))
                value (as/subscribe #_ctx [::value])]
    (js/console.log "Component" (rc/current-component) value)
    [examples-select {:examples           aed/all
                      :value              value
                      :on-example-changed (fn [i]
                                            (let [new-value (-> i .-target .-value)]
                                              (js/console.log "Example change value" new-value)
                                              (as/dispatch #_ctx [::change-value new-value])))}]))

(defn init []
  (if-let [el (dc/sel1 "#examples-select")]
    (rc/render #_auc/app examples-select-state el)))