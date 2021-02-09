using System.IO;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Threading.Tasks;

namespace In3.Transport
{
    /// <summary>
    /// Basic implementation for synchronous http transport for Incubed client.
    /// </summary>
    public class DefaultTransport : Transport
    {
        private readonly HttpClient _client = new HttpClient();

        /// <summary>
        /// Standard construction.
        /// </summary>
        public DefaultTransport()
        {
            _client.DefaultRequestHeaders.Accept.Add(new MediaTypeWithQualityHeaderValue("application/json"));
        }

        /// <summary>
        /// Method that handles, sychronously the http requests.
        /// </summary>
        /// <param name="url">The url of the node.</param>
        /// <param name="payload">Json for the body of the POST request to the node.</param>
        /// <returns>The http json response.</returns>
        public async Task<string> Handle(string method, string url, string payload, string[] headers)
        {
            var httpWebRequest = (HttpWebRequest)WebRequest.Create(url);
            httpWebRequest.ContentType = "application/json";
            httpWebRequest.Method = method;
            ServicePointManager.SecurityProtocol = SecurityProtocolType.Tls13 | SecurityProtocolType.Tls12 | SecurityProtocolType.Tls11 | SecurityProtocolType.Tls;
            foreach (var header in headers) httpWebRequest.Headers.Add(header);

            using (var streamWriter = new StreamWriter(httpWebRequest.GetRequestStream()))
            {
                streamWriter.Write(payload);
                streamWriter.Flush();
                streamWriter.Close();
            }

            var httpResponse = (HttpWebResponse) await httpWebRequest.GetResponseAsync();
            using (var streamReader = new StreamReader(httpResponse.GetResponseStream()))
            {
                return streamReader.ReadToEnd();
            }
        }
    }
}