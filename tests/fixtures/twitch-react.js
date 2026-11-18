// Minimal controlled-input version of Twitch's public prediction checkout.
const React=require('react'),{createRoot}=require('react-dom/client');
const h=React.createElement;
window.votes=[];
function App() {
  const [show,setShow]=React.useState(false),[first,setFirst]=React.useState(''),[second,setSecond]=React.useState('');
  const field=(index,value,setValue)=>h('div',{className:'custom-prediction-button',key:index},
    h('input',{type:'number',value,onChange:event=>setValue(event.currentTarget.value)}),
    h('button',{onClick:()=>window.votes.push({index,amount:value})},'Vote'));
  return h('div',{className:'chat-room','data-test-selector':'chat-room-component-layout'},
    h('div',{className:'prediction-checkout-details-header'},h('strong',null,'Will it happen?'),h('p',null,'Open')),
    h('div',{'data-test-selector':'prediction-summary-outcome__title'},'Yes'),
    h('div',{'data-test-selector':'prediction-summary-outcome__title'},'No'),
    h('button',{'data-test-selector':'prediction-checkout-active-footer__input-type-toggle',onClick:()=>setShow(true)},'Custom'),
    show ? h('div',null,field(0,first,setFirst),field(1,second,setSecond)) : null);
}
createRoot(document.getElementById('react-root')).render(h(App));
