import { createServer } from 'node:http';

const HOST = process.env.HOST ?? '127.0.0.1';
const PORT = Number(process.env.PORT ?? 3000);

const server = createServer((req, res) => {
  const now = new Date().toISOString();
  const method = req.method ?? 'UNKNOWN';
  const url = req.url ?? '/';

  // Gossip log for each inbound request.
  console.log(`[gossip] ${now} ${method} ${url}`);

  if (url === '/health') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ status: 'ok' }));
    return;
  }

  res.writeHead(200, { 'Content-Type': 'text/plain' });
  res.end('Hello World!\n');
});

server.listen(PORT, HOST, () => {
  console.log(`Listening on ${HOST}:${PORT}`);
});

// run with `node server.mjs`
