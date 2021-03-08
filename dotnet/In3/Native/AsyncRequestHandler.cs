using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading.Tasks;

namespace In3.Native
{
    internal class AsyncRequestHandler
    {
        private NativeClient NativeClient { get; }

        public AsyncRequestHandler(NativeClient client)
        {
            NativeClient = client;
        }

        public async Task Handle(IntPtr ctx)
        {
            IntPtr reqPtr = in3_create_request(ctx);
            int urlsLength = in3_get_request_urls_len(reqPtr);
            int payloadLength = in3_get_request_payload_len(reqPtr);

            // This is marshaled in a non-declarative way to prevent double freeing of the string (sometimes necessary when the string is declared as a char*).
            byte[] payload = new byte[payloadLength];
            Marshal.Copy(in3_get_request_payload(reqPtr), payload, 0, payloadLength);
            string method = Marshal.PtrToStringAnsi(in3_get_request_method(reqPtr));
            string[] urls = Utils.GetAllStrings(in3_get_request_urls(reqPtr), urlsLength);
            string[] headers = new string[in3_get_request_headers_len(reqPtr)];
            for (int i=0;i<headers.Length;i++) headers[i] = Marshal.PtrToStringAnsi(in3_get_request_headers_at(reqPtr,i));


            IEnumerable<Task> requestsTasks = urls.Select(async (url, i) =>
            {
                try
                {
                    string result = await NativeClient.Client.Transport.Handle(method, urls[i], payload, headers);
                    // This is freed here (by the declartive marshall) and it works because internally this is an sb_add which copies the string (same with error).
                    in3_req_add_response(reqPtr, i, false, result, result.Length,0);
                }
                catch (Exception ex)
                {
                    in3_req_add_response(reqPtr, i, true, ex.Message, ex.Message.Length,0);
                }
            });

            await Task.WhenAll(requestsTasks);
            request_free(reqPtr, NativeClient.Pointer, false);
        }

        [DllImport("libin3", CharSet = CharSet.Ansi)] private static extern IntPtr in3_get_request_method(IntPtr request);
        [DllImport("libin3", CharSet = CharSet.Ansi)] private static extern IntPtr in3_get_request_payload(IntPtr request);
        [DllImport("libin3", CharSet = CharSet.Ansi)] private static extern int in3_get_request_payload_len(IntPtr request);
        [DllImport("libin3", CharSet = CharSet.Ansi)] private static extern IntPtr in3_get_request_urls(IntPtr request);
        [DllImport("libin3", CharSet = CharSet.Ansi)] private static extern int in3_get_request_urls_len(IntPtr request);
        [DllImport("libin3", CharSet = CharSet.Ansi)] private static extern int in3_get_request_headers_len(IntPtr request);
        [DllImport("libin3", CharSet = CharSet.Ansi)] private static extern IntPtr in3_get_request_headers_at(IntPtr request, int index);
        [DllImport("libin3", CharSet = CharSet.Ansi)] private static extern void in3_req_add_response(IntPtr req, int index, bool is_error, string data, int data_len, int time);
        [DllImport("libin3", CharSet = CharSet.Ansi)] private static extern void request_free(IntPtr req, IntPtr ctx, bool free_response);
        [DllImport("libin3", CharSet = CharSet.Ansi)] private static extern IntPtr in3_create_request(IntPtr ctx);
    }
}