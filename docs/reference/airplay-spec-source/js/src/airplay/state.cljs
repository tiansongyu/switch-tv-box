(ns airplay.state
  (:require [react :as react]
            [reagent.core :as rc]))


(defonce context (react/createContext "airplay-state"))
(def Provider (.-Provider context))
(def Consumer (.-Consumer context))

(deftype App [db]
  ILookup
  (-lookup [o k]
    (case k
      :db db))
  (-lookup [o k not-found]
    (case k
      :db db
      not-found))
  IHash
  (-hash [this] (goog/getUid this)))
(defn create-app [initial-value]
  (->App (rc/atom initial-value)))

(defn render-app [app component container]
  (js/console.log "Rendering app" app (:db app))
  (rc/render [:> Provider {:value app}
              [component]
              #_[:> Consumer {}
               (fn [v]
                 (rc/as-element [:div "Context: " (get-in v [:db :current :features]) [component]]))]]
             container))


(defonce subscriptions (atom {}))

(defn reg-sub [id query-fn]
  (swap! subscriptions assoc id query-fn))

(defonce events (atom {}))

(defn reg-event-fx [id effect-fn]
  (swap! events assoc id effect-fn))

(defn subscribe [ctx query]
  (let [query-fn (get @subscriptions (first query))
        db (:db ctx)]
    (js/console.log "subscribe Context" ctx query db)
    (assert query-fn
            (str "first value of query must be registered subscription "
                 (pr-str (first query))))
    (rc/cursor #(query-fn @db %) query)))


(defn dispatch [ctx event]
  (let [effect-fn (get @events (first event))
        db (:db ctx)]
    (js/console.log "dispatch Context" ctx event db)
    (assert effect-fn
            (str "first value of event must be registered event-fx "
                 (pr-str (first event))))
    (swap! db (fn [old] (:db (effect-fn {:db old} event))))))

(comment
  @subscriptions)