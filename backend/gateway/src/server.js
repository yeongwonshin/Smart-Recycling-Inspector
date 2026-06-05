import path from 'node:path';
import { fileURLToPath } from 'node:url';
import express from 'express';
import cors from 'cors';
import morgan from 'morgan';
import multer from 'multer';
import { v4 as uuidv4 } from 'uuid';
import grpc from '@grpc/grpc-js';
import protoLoader from '@grpc/proto-loader';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const repoRoot = path.resolve(__dirname, '../../..');
const protoPath = path.join(repoRoot, 'proto', 'recycling.proto');

const packageDefinition = protoLoader.loadSync(protoPath, {
  keepCase: false,
  longs: String,
  enums: String,
  defaults: true,
  oneofs: true,
  arrays: true,
  objects: true,
  bytes: Buffer,
});

const recyclingProto = grpc.loadPackageDefinition(packageDefinition).recycling;
const grpcTarget = process.env.GRPC_TARGET || 'localhost:50051';
const client = new recyclingProto.RecyclingInspector(
  grpcTarget,
  grpc.credentials.createInsecure(),
  {
    'grpc.max_receive_message_length': 64 * 1024 * 1024,
    'grpc.max_send_message_length': 64 * 1024 * 1024,
  }
);

const app = express();
const upload = multer({
  storage: multer.memoryStorage(),
  limits: {
    fileSize: 12 * 1024 * 1024,
    files: 128,
  },
});

app.use(cors());
app.use(morgan('dev'));
app.use(express.json());

app.get('/api/health', (_req, res) => {
  res.json({ ok: true, gateway: 'ready', grpcTarget });
});

app.post('/api/analyze', upload.array('images', 128), async (req, res) => {
  try {
    const files = req.files || [];
    if (files.length === 0) {
      return res.status(400).json({ error: 'At least one image file is required.' });
    }

    const threadCount = Number.parseInt(req.body.thread_count || '4', 10);
    const batchSize = Number.parseInt(req.body.batch_size || String(files.length), 10);
    const requestId = uuidv4();

    const grpcRequest = {
      requestId,
      threadCount: Number.isFinite(threadCount) && threadCount > 0 ? threadCount : 4,
      batchSize: Number.isFinite(batchSize) && batchSize > 0 ? batchSize : files.length,
      images: files.map((file, index) => ({
        imageId: `${requestId}_${index}`,
        filename: file.originalname,
        data: file.buffer,
      })),
    };

    client.AnalyzeImages(grpcRequest, (err, response) => {
      if (err) {
        console.error(err);
        return res.status(502).json({
          error: 'gRPC inspection server failed.',
          details: err.message,
        });
      }
      return res.json(response);
    });
  } catch (error) {
    console.error(error);
    return res.status(500).json({ error: 'Gateway error', details: error.message });
  }
});

const port = Number.parseInt(process.env.PORT || '8080', 10);
app.listen(port, '0.0.0.0', () => {
  console.log(`Smart Recycling Gateway listening on http://0.0.0.0:${port}`);
  console.log(`Forwarding gRPC requests to ${grpcTarget}`);
});
