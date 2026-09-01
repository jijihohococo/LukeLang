(function(){const s=document.createElement("link").relList;if(s&&s.supports&&s.supports("modulepreload"))return;for(const t of document.querySelectorAll('link[rel="modulepreload"]'))i(t);new MutationObserver(t=>{for(const a of t)if(a.type==="childList")for(const c of a.addedNodes)c.tagName==="LINK"&&c.rel==="modulepreload"&&i(c)}).observe(document,{childList:!0,subtree:!0});function n(t){const a={};return t.integrity&&(a.integrity=t.integrity),t.referrerPolicy&&(a.referrerPolicy=t.referrerPolicy),t.crossOrigin==="use-credentials"?a.credentials="include":t.crossOrigin==="anonymous"?a.credentials="omit":a.credentials="same-origin",a}function i(t){if(t.ep)return;t.ep=!0;const a=n(t);fetch(t.href,a)}})();const f="/ops/api";async function l(e){const s=await fetch(`${f}${e}`,{cache:"no-store"});if(!s.ok)throw new Error(`${e} → ${s.status}`);return s.json()}const d={overview:()=>l("/overview"),services:()=>l("/services"),metrics:()=>l("/metrics"),incidents:()=>l("/incidents")},g=document.querySelector("#app"),_=5e3;function u(e){return e>=90?"metric__value--bad":e>=75?"metric__value--warn":"metric__value--ok"}function $(e){return e==="up"?"state state--up":e==="degraded"?"state state--degraded":e==="down"?"state state--down":"state state--unknown"}function y(e){return e==="up"?"Operational":e==="degraded"?"Degraded":e==="down"?"Down":"Unknown"}function b(e,s){const n=s.some(t=>t.status==="down"),i=s.some(t=>t.status==="degraded");return n?"Some systems <em>down</em>":i?"Partial <em>degradation</em>":"All systems <em>operational</em>"}function k(e,s){return e.length?[...e].reverse().slice(-60).map(i=>{const t=s(i),a=Math.max(8,Math.round(t/100*72)),c=t>=90?"bad":t>=75?"warn":"ok";return`<i style="height:${a}px" data-level="${c}" title="${t.toFixed(1)}%"></i>`}).join(""):""}function p(e,s,n,i,t,a){const c=s.map(r=>`<li>
        <b>${o(r.name)}</b>
        <span>${o(r.description)}${r.latency_ms?` · ${r.latency_ms}ms`:""}</span>
        <em class="${$(r.status)}">${y(r.status)}</em>
      </li>`).join(""),v=i.length===0?'<p class="empty"><b>No incidents recorded.</b> Open issues appear here with status and timeline.</p>':`<ul class="components">${i.map(r=>`<li>
              <b>${o(r.title)}</b>
              <span>${o(r.summary||r.status)}</span>
              <em class="${r.status==="resolved"?"state state--up":"state state--degraded"}">${o(r.status)}</em>
            </li>`).join("")}</ul>`;g.innerHTML=`
    <div class="grain" aria-hidden="true"></div>
    <header class="top">
      <a class="top__brand" href="https://lukelang.org">
        <img src="https://lukelang.org/assets/luke-mark-sm.png" alt="" width="32" height="32" />
        <span>LukeLang</span>
      </a>
      <nav class="top__links" aria-label="Primary">
        <a href="https://lukelang.org">Home</a>
        <a href="https://status.lukelang.org">Status</a>
        <a href="https://lukelang.org/docs/">Docs</a>
        <a href="https://github.com/lucasdmarshall/LukeLang/tree/main/apps/devops">Source</a>
      </nav>
    </header>

    <section class="head">
      <p class="head__kicker">DevOps · LukeLang backend</p>
      <h1><span class="beacon" aria-hidden="true"></span> ${b(e,s)}</h1>
      <p>
        Live host metrics and service checks from Postgres — API served by a compiled LukeLang binary.
        ${a?`<br/><strong style="color:var(--red)">${o(a)}</strong>`:""}
      </p>
    </section>

    <main>
      <section>
        <h2>Host <em>metrics</em></h2>
        <div class="metrics">
          <div class="metric">
            <div class="metric__label">CPU</div>
            <div class="metric__value ${u(e.cpu)}">${m(e.cpu)}%</div>
          </div>
          <div class="metric">
            <div class="metric__label">Memory</div>
            <div class="metric__value ${u(e.mem)}">${m(e.mem)}%</div>
          </div>
          <div class="metric">
            <div class="metric__label">Disk</div>
            <div class="metric__value ${u(e.disk)}">${m(e.disk)}%</div>
          </div>
          <div class="metric">
            <div class="metric__label">Load (1m)</div>
            <div class="metric__value">${e.load1.toFixed(2)}</div>
          </div>
          <div class="metric">
            <div class="metric__label">Services up</div>
            <div class="metric__value metric__value--ok">${e.services_up??"—"}/${e.services_total}</div>
          </div>
        </div>
        <p class="meta">Last sample: ${e.recorded_at?o(e.recorded_at):"—"} · refreshed ${t.toISOString().replace("T"," ").slice(0,19)} UTC</p>
      </section>

      <section>
        <h2>CPU <em>24h</em></h2>
        <div class="chart" aria-hidden="true">${k(n,r=>r.cpu_pct)}</div>
      </section>

      <section>
        <h2>Components</h2>
        <ul class="components">${c}</ul>
      </section>

      <section>
        <h2>Incident <em>history</em></h2>
        ${v}
      </section>
    </main>

    <footer>
      <p>LukeLang DevOps · API: LukeLang + Postgres · UI: TypeScript</p>
      <nav><a href="https://lukelang.org/community/">Report a problem</a></nav>
    </footer>
  `}function m(e){return Number.isFinite(e)?e.toFixed(1):"—"}function o(e){return e.replaceAll("&","&amp;").replaceAll("<","&lt;").replaceAll(">","&gt;").replaceAll('"',"&quot;")}async function h(){try{const[e,s,n,i]=await Promise.all([d.overview(),d.services(),d.metrics(),d.incidents()]);p(e,s,n,i,new Date)}catch(e){p({cpu:0,mem:0,disk:0,load1:0,services_up:0,services_total:0},[],[],[],new Date,e instanceof Error?e.message:"API unreachable")}}h();setInterval(()=>void h(),_);
