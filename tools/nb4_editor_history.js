/* SPDX-License-Identifier: GPL-2.0-only */
class EditorHistory {
  constructor(value, limit = 100) { this.limit=limit; this.reset(value); }
  reset(value) { this.entries=[JSON.stringify(value)]; this.index=0; this.saved=this.entries[0]; this.at=0; }
  push(value, group='', now=Date.now()) {
    const text=JSON.stringify(value); if(text===this.entries[this.index]) return;
    this.entries.splice(this.index+1);
    if(group && group===this.group && now-this.at<600 && this.index>0 && this.entries[this.index]!==this.saved) this.entries[this.index]=text;
    else { this.entries.push(text); this.index++; }
    if(this.entries.length>this.limit+1) {this.entries.shift();this.index--;}
    this.group=group;this.at=now;
  }
  undo() {if(this.index>0)this.index--;this.group='';return JSON.parse(this.entries[this.index]);}
  redo() {if(this.index+1<this.entries.length)this.index++;this.group='';return JSON.parse(this.entries[this.index]);}
  markSaved() {this.saved=this.entries[this.index];}
  get dirty() {return this.entries[this.index]!==this.saved;}
}
if(typeof module!=='undefined')module.exports=EditorHistory;
