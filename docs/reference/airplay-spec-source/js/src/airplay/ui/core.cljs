(ns airplay.ui.core
  (:require [re-frame.core :as as]
            [airplay.examples.data :as aed]
            [re-frame.core :as rf]))

#_(defonce app (as/create-app {:current {:features ""}}))

(rf/reg-event-db              ;; sets up initial application state
  :initialize                 ;; usage:  (dispatch [:initialize])
  (fn [_ _]                   ;; the two parameters are not important here, so use _
    (js/console.log "initialize")
    {:current {:features ""}         ;; What it returns becomes the new application state
     :examples nil
     :time-color "#f88"}))    ;; so the application state will initially be a map with two keys

(rf/reg-event-fx ::current-changed
                 (fn [{:keys [db]} _]
                   (let [current (:current db)
                         example (some (fn [[k v]]
                                         (js/console.log "compare" k (= current v) v current)
                                         (if (= current v) k)
                                         ) aed/all)]
                     (js/console.log "current changed " (pr example))
                     {:db (assoc db :example example)})))
