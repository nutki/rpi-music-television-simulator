import React from 'react';
import MaterialTable from '@material-table/core';
import AppBar from "@material-ui/core/AppBar";
import Tabs from "@material-ui/core/Tabs";
import Tab from "@material-ui/core/Tab";
import TextField from '@material-ui/core/TextField';
import Badge from '@material-ui/core/Badge';

const columns = [
  { title: 'Artist', field: 'meta.artist' },
  { title: 'Title', field: 'meta.name',
    customFilterAndSearch: (f, r) =>
      (r.name?.toLowerCase().includes(f.toLowerCase()) || r.id?.toLowerCase().includes(f.toLowerCase()))
  },
  { title: 'Album', field: 'meta.album' },
  { title: 'Year', field: 'meta.year', /*type: 'numeric'*/ },
  { title: 'Director', field: 'meta.director' },
  { title: 'Label', field: 'meta.label', searchable: false },
  { title: 'Added', field: 'videoTimeStamp', editable: 'never',
    render: a => tsToString(a.videoTimeStamp),
    customSort: (a, b) => (a.videoTimeStamp||0)-(b.videoTimeStamp||0),
  }
];

async function postData(url = '', data = {}) {
  // Default options are marked with *
  const response = await fetch(url, {
    method: 'POST',
    cache: 'no-cache',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(data)
  });
  return response.json(); // parses JSON response into native JavaScript objects
}

const uri = (strings, ...values) =>
  strings.map((s, i) => s + (values[i] === undefined ? '' : encodeURIComponent(values[i]))).join('');

// Dan Abramov's useInterval hook
function useInterval(callback, delay) {
  const savedCallback = React.useRef();

  // Remember the latest callback.
  React.useEffect(() => {
    savedCallback.current = callback;
  }, [callback]);

  // Set up the interval.
  React.useEffect(() => {
    function tick() {
      savedCallback.current();
    }
    if (delay !== null) {
      let id = setInterval(tick, delay);
      return () => clearInterval(id);
    }
  }, [delay]);
}


import Button from '@material-ui/core/Button';
import Dialog from '@material-ui/core/Dialog';
import DialogActions from '@material-ui/core/DialogActions';
import DialogContent from '@material-ui/core/DialogContent';
import DialogContentText from '@material-ui/core/DialogContentText';
import DialogTitle from '@material-ui/core/DialogTitle';
import CircularProgress from '@material-ui/core/CircularProgress';

function FormDialog({ open, value, onConfirm, onClose, onChange}) {
  return (
      <Dialog open={open} onClose={onClose} aria-labelledby="form-dialog-title">
        <DialogTitle id="form-dialog-title">Add Download URLs</DialogTitle>
        <DialogContent>
          <DialogContentText>
            Paste video URLs to download below
          </DialogContentText>
          <TextField
            autoFocus
            margin="dense"
            id="name"
            label=""
            multiline
            fullWidth
            value={value}
            onChange={ev => onChange(ev.target.value)}
          />
        </DialogContent>
        <DialogActions>
          <Button onClick={onClose} color="primary">
            Cancel
          </Button>
          <Button onClick={onConfirm} color="primary">
            Add
          </Button>
        </DialogActions>
      </Dialog>
  );
}
function QueryDialog({ id, onClose, onRowUpdate }) {
  const [ query, setQuery ] = React.useState(id);
  const [ data, setData ] = React.useState();
  const load = () => void fetch(uri`/api/query/${query}`).then(r => r.json()).then(r => setData(r));
  return (
      <Dialog open onClose={onClose} aria-labelledby="form-dialog-title">
        <DialogTitle id="form-dialog-title">Search for Details on Wikipedia</DialogTitle>
        <DialogContent>
          <DialogContentText>
            Search query
          </DialogContentText>
          <TextField
            autoFocus
            margin="dense"
            id="name"
            label=""
            multiline
            fullWidth
            value={query}
            onChange={ev => setQuery(ev.target.value)}
          />
          {data && <ul>
            <li><b>Artist:</b> {data.artist}</li>
            <li><b>Title:</b> {data.name}</li>
            <li><b>Album:</b> {data.album}</li>
            <li><b>Year:</b> {data.year}</li>
            <li><b>Director:</b> {data.director}</li>
            <li><b>Label:</b> {data.label}</li>
            <li><b>Genre:</b> {data.genres?.join(" ")}</li>
          </ul>}
        </DialogContent>
        <DialogActions>
          <Button onClick={load} color="primary">
            Search
          </Button>
          <Button onClick={onClose} color="primary">
            Cancel
          </Button>
          <Button onClick={() => { onRowUpdate({id, meta: data}); onClose(); }} disabled={!data} color="primary">
            Use
          </Button>
        </DialogActions>
      </Dialog>
  );
}
function DownloadModal({open, setOpen}) {
  const [value, setValue] = React.useState("");
  const handleConfirm = () => {
    postData('/api/download', { urls: value.split('\n').filter(Boolean) });
    setValue("");
    setOpen(false);
  };
  const handleClose = () => {
    setValue("");
    setOpen(false);
  }
  return <>
    <FormDialog
      open={open}
      value={value}
      onClose={handleClose}
      onConfirm={handleConfirm}
      onChange={setValue}
    />
  </>;
}
function useDownloads() {
  const [data, setData] = React.useState();
  const load = () => void fetch('/api/download').then(r => r.json()).then(r => setData(r));
  React.useEffect(load, []);
  useInterval(load, 1000);
  return data;
}
function DownloadsTable({onAdd, data}) {
  return <>
    <MaterialTable
      title="Pending Downloads"
      localization={{body: {emptyDataSourceMessage: "No pending downloads"}}}
      columns={[
        { title: "URL", field: "url" },
        { title: "File Name", field: "name" },
        { title: "Progress", field: "progress", width: 200,
          render: (data) => <CircularProgress value={data.progress} variant="static"/>
        },
      ]}
      isLoading={!data}
      data={data}
      actions={[
        {
          onClick: onAdd,
          icon: 'add',
          tooltip: "Add URLs",
          isFreeAction: true,
        }
      ]}
      options={{
        paging: false,
        sorting: false,
        search: false,
      }}
    />
  </>;
}

const dateComponentFomatters = [
  new Intl.DateTimeFormat('en', { year: 'numeric' }),
  new Intl.DateTimeFormat('en', { month: '2-digit' }),
  new Intl.DateTimeFormat('en', { day: '2-digit' }),
]
function tsToString(ts) {
  if (!ts) return;
  const date = new Date(ts);
  return dateComponentFomatters.map(f => f.format(date)).join('-');
}


function MetaDialog({ open, value, onConfirm, onClose, onChange}) {
  return (
      <Dialog open={open} onClose={onClose} aria-labelledby="form-dialog-title">
        <DialogTitle id="form-dialog-title">Search Wikipedia for meta data</DialogTitle>
        <DialogContent>
          <DialogContentText>
            Search key
          </DialogContentText>
          <TextField
            autoFocus
            margin="dense"
            id="name"
            label=""
            multiline
            fullWidth
            value={value}
            onChange={ev => onChange(ev.target.value)}
          />
        </DialogContent>
        <DialogActions>
          <Button onClick={onClose} color="primary">
            Cancel
          </Button>
          <Button onClick={onConfirm} color="primary">
            Search
          </Button>
        </DialogActions>
      </Dialog>
  );
}

function VideoDetail({id, onRowUpdate}) {
  const [data, setData] = React.useState();
  const [dialogOpen, setDialogOpen] = React.useState(false);
  const load = () => void fetch(uri`/api/video/${id}`).then(r => r.json()).then(r => setData(r));
  React.useEffect(load, []);
  if (!data) return <div style={{
    display: "flex",
    alignItems: "center",
    justifyContent: "center",
    height: 300,
  }}><CircularProgress/></div>;
  return <><div style={{
    display: "grid",
    gridTemplateColumns: "50% auto",
  }}>
    <ul>
      <li>File name: {data.id}</li>
      <li>Resolution: {data.videoWidth}x{data.videoHeight}</li>
      <li>Size: {Math.round(data.videoSize/1024/1024)}MB</li>
      <li>Bitrate: {Math.round(data.videoBitrate/1024*10)/10}Mb/s</li>
      <li>Length: {Math.floor(data.videoLength/60)}m{Math.floor(data.videoLength%60)}s</li>
      <li>Added: {tsToString(data.videoTimeStamp)}</li>
      <li><Button onClick={() => setDialogOpen(true)}>Find meta</Button></li>
    </ul>
    <video style={{justifySelf: "right"}} controls height="300">
      {data.video && <source src={"/videos/" + data.video} type="video/mp4"/>}
    </video>
  </div>{dialogOpen && <QueryDialog id={data.id} onRowUpdate={onRowUpdate} onClose={() => setDialogOpen(false)}/>}</>;
}

function Panel({visible, children}) {
  return <div hidden={!visible}>{children}</div>;
}

let cnt = 0;



export default function MaterialTableDemo() {
  const [open, setOpen] = React.useState(false);
  const [data, setData] = React.useState();
  const [panelIdx, setPanelIdx] = React.useState(0);
  console.log('Rendering', cnt++);
  const load = () => void fetch('/api/list').then(r => r.json()).then(r => setData(r));
  React.useEffect(load, []);
  useInterval(load, 5000);
  const downloads = useDownloads();

  async function onRowUpdate(newData) {
    await postData('/api/update', newData);
    setData((prevState) => prevState.map(row => row.id === newData.id ? { ...row, ...newData } : row));
  }
  async function onRowDelete(oldData) {
    await postData('/api/delete', { id: oldData.id });
    setData((prevState) => prevState.filter(row => row.id != oldData.id));
  }
  const panel0 = <Panel visible={panelIdx === 0}>
    <MaterialTable
      title=""
      columns={columns}
      isLoading={!data}
      data={data}
      options={{
        pageSize:10,
        pageSizeOptions:[],
      }}
      actions={[
        {
          onClick: (ev, r) => void fetch(uri`/api/video/${r.id}/play`),
          icon: 'play_arrow',
          tooltip: 'Play',
        },
        {
          onClick: load,
          icon: 'refresh',
          tooltip: 'Reload',
          isFreeAction: true,
        },
        {
          onClick: () => setOpen(true),
          icon: 'add',
          tooltip: "Add Download URLs",
          isFreeAction: true,
        }
      ]}
      editable={{
        onRowUpdate,
        onRowDelete,
      }}
      detailPanel={row => <VideoDetail id={row.rowData.id} onRowUpdate={onRowUpdate}/>}
    />
  </Panel>;
  const panel2 = <Panel visible={panelIdx === 2}>
    <DownloadsTable onAdd={() => setOpen(true)} data={downloads}/>
  </Panel>;
  const downloadCount = downloads?.length || undefined;

  return (
    <>
      <AppBar position="static">
        <Tabs
          value={panelIdx}
          onChange={(ev, val) => {
            setPanelIdx(val);
            if (val === 0) load();
          }}
        >
          <Tab label="Videos" />
          <Tab label="Channels" />
          <Tab label={<Badge badgeContent={downloadCount} color="secondary">Downloads</Badge>} />
        </Tabs>
      </AppBar>
      {panel0}
      {panel2}
      <DownloadModal open={open} setOpen={setOpen} />
    </>
  );
}
