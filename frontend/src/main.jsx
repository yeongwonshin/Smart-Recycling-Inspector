import React, { useMemo, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { UploadCloud, Cpu, Network, Sparkles, Recycle, Gauge, CheckCircle2, AlertTriangle } from 'lucide-react';
import './styles.css';

const API_BASE = import.meta.env.VITE_API_BASE_URL || 'http://localhost:8080';

function pct(v) {
  const n = Number(v || 0);
  return `${(n * 100).toFixed(1)}%`;
}

function ms(v) {
  const n = Number(v || 0);
  return `${n.toFixed(2)} ms`;
}

function categoryLabel(category) {
  const labels = {
    plastic: 'Plastic',
    paper: 'Paper',
    can: 'Metal Can',
    glass: 'Glass',
    food_waste: 'Food Waste',
    general_waste: 'General Waste',
    unknown: 'Unknown',
  };
  return labels[category] || category;
}

function contaminationTone(status) {
  if (status === 'clean_or_low_risk') return 'good';
  if (status === 'unknown') return 'neutral';
  return 'warn';
}

function FeatureBar({ name, value }) {
  const safe = Math.max(0, Math.min(1, Number(value || 0)));
  return (
    <div className="feature-row">
      <div className="feature-label">{name.replaceAll('_', ' ')}</div>
      <div className="feature-track">
        <div className="feature-fill" style={{ width: `${safe * 100}%` }} />
      </div>
      <div className="feature-value">{pct(safe)}</div>
    </div>
  );
}

function ResultCard({ result, previewUrl }) {
  const tone = contaminationTone(result.contaminationStatus);
  return (
    <article className="result-card">
      <div className="result-header">
        {previewUrl ? <img src={previewUrl} alt={result.filename} className="thumb" /> : <div className="thumb placeholder" />}
        <div>
          <h3>{result.filename}</h3>
          <p className="muted">{result.ok ? 'Analyzed successfully' : result.errorMessage}</p>
        </div>
      </div>

      <div className="prediction-grid">
        <div className="metric-box">
          <span>Predicted bin</span>
          <strong>{categoryLabel(result.category)}</strong>
        </div>
        <div className="metric-box">
          <span>Confidence</span>
          <strong>{pct(result.confidence)}</strong>
        </div>
        <div className={`metric-box ${tone}`}>
          <span>Contamination</span>
          <strong>{result.contaminationStatus?.replaceAll('_', ' ')}</strong>
        </div>
        <div className="metric-box">
          <span>Total image time</span>
          <strong>{ms(result.breakdown?.totalMs)}</strong>
        </div>
      </div>

      <section className="explain-box">
        <h4><Sparkles size={16} /> Why this result?</h4>
        <p>{result.explanation}</p>
      </section>

      <section className="recommend-box">
        <h4>{tone === 'warn' ? <AlertTriangle size={16} /> : <CheckCircle2 size={16} />} Recommendation</h4>
        <p>{result.recommendation}</p>
        <ol>
          {(result.disposalSteps || []).map((step, idx) => <li key={idx}>{step}</li>)}
        </ol>
      </section>

      <section>
        <h4><Gauge size={16} /> Feature signals</h4>
        {(result.features || []).map((f) => <FeatureBar key={f.name} name={f.name} value={f.value} />)}
      </section>

      <section>
        <h4><Cpu size={16} /> Processing breakdown</h4>
        <div className="breakdown-grid">
          <span>Decode</span><b>{ms(result.breakdown?.decodeMs)}</b>
          <span>Preprocess</span><b>{ms(result.breakdown?.preprocessMs)}</b>
          <span>Feature</span><b>{ms(result.breakdown?.featureMs)}</b>
          <span>Inference</span><b>{ms(result.breakdown?.inferenceMs)}</b>
          <span>Postprocess</span><b>{ms(result.breakdown?.postprocessMs)}</b>
        </div>
      </section>
    </article>
  );
}

function App() {
  const [files, setFiles] = useState([]);
  const [threadCount, setThreadCount] = useState(4);
  const [batchSize, setBatchSize] = useState(16);
  const [loading, setLoading] = useState(false);
  const [response, setResponse] = useState(null);
  const [error, setError] = useState('');

  const previews = useMemo(() => {
    const map = new Map();
    files.forEach((file) => map.set(file.name, URL.createObjectURL(file)));
    return map;
  }, [files]);

  async function analyze() {
    setError('');
    setResponse(null);
    if (!files.length) {
      setError('Upload at least one image.');
      return;
    }
    const form = new FormData();
    files.forEach((file) => form.append('images', file));
    form.append('thread_count', String(threadCount));
    form.append('batch_size', String(batchSize));

    setLoading(true);
    try {
      const res = await fetch(`${API_BASE}/api/analyze`, { method: 'POST', body: form });
      const data = await res.json();
      if (!res.ok) throw new Error(data.error || 'Request failed');
      setResponse(data);
    } catch (err) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  }

  return (
    <main>
      <section className="hero">
        <div className="badge"><Recycle size={16} /> AI + gRPC + OpenMP</div>
        <h1>Smart Recycling Inspection Pipeline</h1>
        <p>
          Upload waste images and run a distributed AI inspection pipeline. The React dashboard calls a Node gateway,
          the gateway forwards images through gRPC, and the C++ server uses OpenMP to process image batches in parallel.
        </p>
      </section>

      <section className="panel">
        <div className="panel-title">
          <UploadCloud />
          <div>
            <h2>Batch Image Upload</h2>
            <p>Use multiple images to make OpenMP acceleration visible in the experiment.</p>
          </div>
        </div>

        <label className="dropzone">
          <input
            type="file"
            accept="image/*"
            multiple
            onChange={(e) => setFiles(Array.from(e.target.files || []))}
          />
          <strong>Choose images</strong>
          <span>{files.length ? `${files.length} file(s) selected` : 'PNG/JPEG images recommended'}</span>
        </label>

        <div className="controls">
          <label>
            <span><Cpu size={16} /> OpenMP thread count</span>
            <input type="range" min="1" max="16" value={threadCount} onChange={(e) => setThreadCount(Number(e.target.value))} />
            <b>{threadCount}</b>
          </label>
          <label>
            <span><Network size={16} /> Logical batch size</span>
            <input type="range" min="1" max="128" value={batchSize} onChange={(e) => setBatchSize(Number(e.target.value))} />
            <b>{batchSize}</b>
          </label>
        </div>

        <button className="primary" disabled={loading} onClick={analyze}>
          {loading ? 'Analyzing...' : 'Run distributed inspection'}
        </button>

        {error && <div className="error">{error}</div>}
      </section>

      {response && (
        <section className="summary-grid">
          <div className="summary-card">
            <span>Images</span>
            <strong>{response.imageCount}</strong>
          </div>
          <div className="summary-card">
            <span>OpenMP threads</span>
            <strong>{response.threadCount}</strong>
          </div>
          <div className="summary-card">
            <span>Server wall time</span>
            <strong>{ms(response.serverWallTimeMs)}</strong>
          </div>
          <div className="summary-card">
            <span>Throughput</span>
            <strong>{Number(response.throughputImagesPerSec || 0).toFixed(2)} img/s</strong>
          </div>
        </section>
      )}

      {response?.results?.length > 0 && (
        <section className="results">
          {response.results.map((result) => (
            <ResultCard
              key={result.imageId}
              result={result}
              previewUrl={previews.get(result.filename)}
            />
          ))}
        </section>
      )}
    </main>
  );
}

createRoot(document.getElementById('root')).render(<App />);
