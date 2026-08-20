// iona-us ローカルファームウェア書き込み用の簡易HTTPサーバー。
// 元々は `python -m http.server` + 手書きHTMLで代替していたものを、
// C#の.slnプロジェクトとして作り直したもの。
// iona.bin と flash_local.html を実行ファイルに埋め込んでいるため、
// このexe単体があれば動く（別途binファイルを持ち歩く必要はない）。
//
// WebUSB(ブラウザ側)がセキュアコンテキストを要求するため、
// http://localhost:<Port>/ で配信する必要がある点は元の構成と同じ。
// ポートは8000から変更（http.sys側にURL予約が残ってしまい衝突したため）。

using System.Diagnostics;
using System.Net;
using System.Reflection;

const int Port = 8080;
var assembly = Assembly.GetExecutingAssembly();

string FindResourceName(string suffix)
{
    var name = assembly.GetManifestResourceNames()
        .FirstOrDefault(n => n.EndsWith(suffix, StringComparison.OrdinalIgnoreCase));
    if (name is null)
    {
        throw new InvalidOperationException(
            $"埋め込みリソースが見つかりません: *{suffix}\n" +
            $"利用可能なリソース: {string.Join(", ", assembly.GetManifestResourceNames())}");
    }
    return name;
}

byte[] ReadResourceBytes(string resourceName)
{
    using var stream = assembly.GetManifestResourceStream(resourceName)
        ?? throw new InvalidOperationException($"リソースを開けません: {resourceName}");
    using var ms = new MemoryStream();
    stream.CopyTo(ms);
    return ms.ToArray();
}

var htmlBytes = ReadResourceBytes(FindResourceName("flash_local.html"));
var binBytes = ReadResourceBytes(FindResourceName("iona.bin"));

var listener = new HttpListener();
listener.Prefixes.Add($"http://localhost:{Port}/");
listener.Start();

Console.WriteLine("iona-us ローカルファームウェア書き込みサーバー");
Console.WriteLine($"埋め込みファームウェア: {binBytes.Length} bytes");

var url = $"http://localhost:{Port}/flash_local.html";
Console.WriteLine($"ブラウザで開きます: {url}");
Console.WriteLine("終了するには Ctrl+C を押してください。");
Console.WriteLine();

try
{
    // UseShellExecute=true で既定のブラウザに委譲する。
    Process.Start(new ProcessStartInfo(url) { UseShellExecute = true });
}
catch (Exception ex)
{
    Console.WriteLine($"ブラウザの自動起動に失敗しました。手動で開いてください: {ex.Message}");
}

while (true)
{
    var context = await listener.GetContextAsync();
    _ = Task.Run(() => HandleRequest(context));
}

void HandleRequest(HttpListenerContext context)
{
    var request = context.Request;
    var response = context.Response;
    try
    {
        var path = request.Url?.AbsolutePath ?? "/";
        byte[] body;
        string contentType;

        if (path is "/" or "/flash_local.html")
        {
            body = htmlBytes;
            contentType = "text/html; charset=utf-8";
        }
        else if (path is "/iona.bin")
        {
            body = binBytes;
            contentType = "application/octet-stream";
        }
        else
        {
            response.StatusCode = 404;
            body = System.Text.Encoding.UTF8.GetBytes("Not Found");
            contentType = "text/plain; charset=utf-8";
        }

        response.ContentType = contentType;
        response.ContentLength64 = body.Length;
        response.OutputStream.Write(body, 0, body.Length);
    }
    catch (Exception ex)
    {
        Console.WriteLine($"リクエスト処理中にエラー: {ex}");
        response.StatusCode = 500;
    }
    finally
    {
        response.OutputStream.Close();
    }
}
