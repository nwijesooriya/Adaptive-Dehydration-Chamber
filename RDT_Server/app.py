from flask import Flask, request, jsonify
import joblib
import pandas as pd

app = Flask(__name__)

# Load the trained Gradient Boosting RDT model
model = joblib.load("rdt_gradient_boosting_pp1.pkl")

FEATURES = [
    "initialMoisturePct",
    "targetMoisturePct",
    "elapsedMin",
    "temperatureC",
    "humidityPct",
    "currentMoisturePct"
]


@app.route("/", methods=["GET"])
def home():
    return jsonify({
        "status": "online",
        "message": "Cinnamon RDT Prediction API"
    })


@app.route("/predict-rdt", methods=["POST"])
def predict_rdt():

    try:
        data = request.get_json()

        if data is None:
            return jsonify({
                "error": "No JSON data received"
            }), 400

        # Make sure every required feature was received
        missing = [
            feature
            for feature in FEATURES
            if feature not in data
        ]

        if missing:
            return jsonify({
                "error": "Missing required features",
                "missing": missing
            }), 400

        # Convert received values into the same format
        # used during model training
        input_data = pd.DataFrame([{
            "initialMoisturePct":
                float(data["initialMoisturePct"]),

            "targetMoisturePct":
                float(data["targetMoisturePct"]),

            "elapsedMin":
                float(data["elapsedMin"]),

            "temperatureC":
                float(data["temperatureC"]),

            "humidityPct":
                float(data["humidityPct"]),

            "currentMoisturePct":
                float(data["currentMoisturePct"])
        }])

        # Predict Remaining Drying Time
        prediction = model.predict(input_data)[0]

        # RDT should never be negative
        prediction = max(0.0, float(prediction))

        return jsonify({
            "remainingDryingTimeMin":
                round(prediction, 1),

            "model":
                "Gradient Boosting PP1",

            "status":
                "preliminary"
        })

    except Exception as error:

        return jsonify({
            "error": str(error)
        }), 500


if __name__ == "__main__":

    # 0.0.0.0 is important because ESP32
    # must access this server through Wi-Fi
    app.run(
        host="0.0.0.0",
        port=5001,
        debug=True
    )