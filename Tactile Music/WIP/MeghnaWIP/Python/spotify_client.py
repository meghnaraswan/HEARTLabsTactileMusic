import requests
import base64
# https://docs.python-requests.org/en/master/user/quickstart/
from conf import config as global_conf
from local import config as local_conf


class spotify_client:
    def get_access_token(self):
        message = local_conf.CLIENT_ID + ":" + local_conf.CLIENT_SECRET
        message_bytes = message.encode('ascii')
        base64_bytes = base64.b64encode(message_bytes)
        message_str = base64_bytes.decode('ascii')
        encoded_client_id_secret = message_str
        headers = {'Authorization':
                       'Basic '+ encoded_client_id_secret}
        r = requests.post(global_conf.ACCESS_TOKEN_URL, data={'grant_type':'client_credentials'},  headers=headers)
        response_json = r.json()
        print(response_json)
        return response_json["access_token"]

    def get_audio_features(self, access_token, track_id):
        headers = {'Authorization':
                       'Bearer '+ access_token}
        r = requests.get(global_conf.AUDIO_FEATURES_API_URL+track_id,  headers=headers)
        response_json = r.json()
        print(response_json)
        return response_json


def main():
    print ("main")
    sfy_cli = spotify_client()
    token = sfy_cli.get_access_token()
    track_id = "7EmsauPLjAy9XMNELaHBZa?="
    audio_features_json = sfy_cli.get_audio_features(token, track_id)
    print("energy:{}, valence:{}".format(audio_features_json["energy"], audio_features_json["valence"]))

    # r = requests.get('https://api.github.com/events')
    # r = requests.post('https://httpbin.org/post', data={'key': 'value'})


if __name__ == '__main__':
    main()
